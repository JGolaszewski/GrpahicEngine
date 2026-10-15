#include "runtime/include/LogEngine.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
class LoggerSession
{
  public:
    explicit LoggerSession(size_t queueCapacity = 128, uint64_t maxFileSizeBytes = 1024ULL * 1024ULL)
    {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                            "_" + std::to_string(s_NextSession.fetch_add(1));
        m_Directory = std::filesystem::temp_directory_path() / ("GraphicEngineLogTests_" + suffix);
        m_Configuration.jsonlPath = m_Directory / "engine.jsonl";
        m_Configuration.breadcrumbPath = m_Directory / "crash.txt";
        m_Configuration.queueCapacity = queueCapacity;
        m_Configuration.maxFileSizeBytes = maxFileSizeBytes;
        m_Configuration.retainedFileCount = 2;
        m_Configuration.minimumLevel = Engine::Log::Level::Trace;
    }

    ~LoggerSession()
    {
        try
        {
            Engine::Log::SetCallback({});
            Engine::Log::Flush();
            Engine::Log::Shutdown();
            std::error_code error;
            std::filesystem::remove_all(m_Directory, error);
        }
        catch (...)
        {
            std::fputs("Failed to clean log engine test workspace.\n", stderr);
        }
    }

    bool Start()
    {
        m_Started = Engine::Log::Initialize(m_Configuration) || Engine::Log::IsInitialized();
        return m_Started;
    }

    const Engine::Log::Configuration &GetConfiguration() const
    {
        return m_Configuration;
    }

  private:
    inline static std::atomic<uint64_t> s_NextSession{0};
    std::filesystem::path m_Directory;
    Engine::Log::Configuration m_Configuration;
    bool m_Started = false;
};

class ScopedEnvironmentVariable
{
  public:
    explicit ScopedEnvironmentVariable(std::string name) : m_Name(std::move(name))
    {
        char *value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, m_Name.c_str()) == 0 && value)
            m_Previous = value;
        std::free(value);
    }

    ~ScopedEnvironmentVariable()
    {
        _putenv_s(m_Name.c_str(), m_Previous ? m_Previous->c_str() : "");
    }

    bool Set(std::string_view value)
    {
        const std::string copy(value);
        return _putenv_s(m_Name.c_str(), copy.c_str()) == 0;
    }

  private:
    std::string m_Name;
    std::optional<std::string> m_Previous;
};

Engine::Log::LogEvent MakeEvent(Engine::Log::Level level, Engine::Log::Category category, const char *format)
{
    Engine::Log::LogEvent event;
    event.level = level;
    event.category = category;
    event.format = format;
    event.file = "LogEngineTests.cpp";
    event.function = "MakeEvent";
    event.line = 42;
    return event;
}

TEST(LogEngineTests, WritesStructuredEventsAndDispatchesCallbacks)
{
    LoggerSession session;
    ASSERT_TRUE(session.Start());
    EXPECT_TRUE(Engine::Log::IsInitialized());
    EXPECT_EQ(Engine::Log::GetHealthSnapshot().fileStatus, Engine::Log::FileSinkStatus::Ready);

    std::mutex callbackMutex;
    std::condition_variable callbackReady;
    Engine::Log::LogRecord callbackRecord;
    bool callbackReceived = false;
    const auto token = Engine::Log::SubscribeCallback([&](const Engine::Log::LogRecord &record) {
        {
            std::lock_guard lock(callbackMutex);
            callbackRecord = record;
            callbackReceived = true;
        }
        callbackReady.notify_one();
    });
    ASSERT_NE(token, 0U);

    Engine::Log::SetFrameIndex(12);
    Engine::Log::LogEvent event =
        MakeEvent(Engine::Log::Level::Info, Engine::Log::Category::Asset, "loaded {} objects");
    event.argumentCount = 1;
    event.arguments[0] = Engine::Log::Detail::MakeArgument(7);
    Engine::Log::Detail::CopyText(event.errorCode, sizeof(event.errorCode), "asset.load.completed");
    event.payload[0] = Engine::Log::MakePayloadEntry("asset_count", 7U);
    event.payloadCount = 1;
    {
        Engine::Log::ScopedOperation operation(99);
        ASSERT_TRUE(Engine::Log::SubmitStructuredEvent(event));
        EXPECT_EQ(Engine::Log::GetCurrentOperationId(), 99U);
    }
    EXPECT_EQ(Engine::Log::GetCurrentOperationId(), 0U);

    Engine::Log::Flush();
    {
        std::unique_lock lock(callbackMutex);
        ASSERT_TRUE(callbackReady.wait_for(lock, std::chrono::seconds(2), [&] { return callbackReceived; }));
    }
    EXPECT_EQ(callbackRecord.text, "loaded 7 objects");
    EXPECT_EQ(callbackRecord.event.frameIndex, 12U);
    EXPECT_EQ(callbackRecord.event.operationId, 99U);
    EXPECT_STREQ(callbackRecord.event.errorCode, "asset.load.completed");

    const Engine::Log::HealthSnapshot health = Engine::Log::GetHealthSnapshot();
    EXPECT_GE(health.acceptedEvents, 1U);
    EXPECT_GE(health.writtenEvents, 1U);
    EXPECT_EQ(health.queueCapacity, 128U);
    EXPECT_FALSE(health.sessionId.empty());

    const auto recent = Engine::Log::GetRecentRecords();
    ASSERT_EQ(recent.size(), 1U);
    EXPECT_EQ(recent.front().text, "loaded 7 objects");
    Engine::Log::DumpCrashBreadcrumbs(session.GetConfiguration().breadcrumbPath);
    EXPECT_TRUE(std::filesystem::exists(session.GetConfiguration().breadcrumbPath));

    std::ifstream jsonl(session.GetConfiguration().jsonlPath);
    const std::string output((std::istreambuf_iterator<char>(jsonl)), std::istreambuf_iterator<char>());
    EXPECT_NE(output.find("asset.load.completed"), std::string::npos);
    EXPECT_NE(output.find("loaded 7 objects"), std::string::npos);
    Engine::Log::UnsubscribeCallback(token);
}

TEST(LogEngineTests, ReportsFileSinkInitializationFailure)
{
    LoggerSession session;
    Engine::Log::Configuration configuration = session.GetConfiguration();
    std::filesystem::create_directories(configuration.jsonlPath.parent_path());
    const auto blockedParent = configuration.jsonlPath.parent_path() / "not-a-directory";
    {
        std::ofstream blocker(blockedParent, std::ios::binary | std::ios::trunc);
        blocker << "block";
        ASSERT_TRUE(blocker.good());
    }
    configuration.jsonlPath = blockedParent / "engine.jsonl";
    configuration.breadcrumbPath = blockedParent / "crash.txt";

    EXPECT_FALSE(Engine::Log::Initialize(configuration));
    EXPECT_TRUE(Engine::Log::IsInitialized());
    const Engine::Log::HealthSnapshot health = Engine::Log::GetHealthSnapshot();
    EXPECT_EQ(health.fileStatus, Engine::Log::FileSinkStatus::Failed);
    EXPECT_NE(health.lastSinkError.find("directory"), std::string::npos);
    EXPECT_TRUE(Engine::Log::Enqueue(
        MakeEvent(Engine::Log::Level::Error, Engine::Log::Category::Diagnostics, "file sink unavailable")));
}

TEST(LogEngineTests, ResolvesValidAndInvalidEnvironmentOverrides)
{
    ScopedEnvironmentVariable minimumLevel("ENGINE_LOG_LEVEL");
    ScopedEnvironmentVariable coreLevel("ENGINE_LOG_CORE_LEVEL");
    ScopedEnvironmentVariable editorLevel("ENGINE_LOG_EDITOR_LEVEL");
    ScopedEnvironmentVariable physicsLevel("ENGINE_LOG_PHYSICS_LEVEL");
    ScopedEnvironmentVariable renderLevel("ENGINE_LOG_RENDER_LEVEL");
    ScopedEnvironmentVariable backendLevel("ENGINE_LOG_BACKEND_LEVEL");
    ScopedEnvironmentVariable audioLevel("ENGINE_LOG_AUDIO_LEVEL");
    ScopedEnvironmentVariable ecsLevel("ENGINE_LOG_ECS_LEVEL");
    ScopedEnvironmentVariable networkLevel("ENGINE_LOG_NET_LEVEL");
    ScopedEnvironmentVariable namedNetworkLevel("ENGINE_LOG_NETWORK_LEVEL");
    ScopedEnvironmentVariable assetLevel("ENGINE_LOG_ASSET_LEVEL");
    ScopedEnvironmentVariable diagnosticsLevel("ENGINE_LOG_DIAGNOSTICS_LEVEL");
    ScopedEnvironmentVariable maxFileSize("ENGINE_LOG_MAX_FILE_SIZE_BYTES");
    ScopedEnvironmentVariable retainedFiles("ENGINE_LOG_RETAINED_FILE_COUNT");
    ScopedEnvironmentVariable breadcrumbs("ENGINE_LOG_BREADCRUMB_PATH");
    ASSERT_TRUE(minimumLevel.Set("warning"));
    ASSERT_TRUE(coreLevel.Set("debug"));
    ASSERT_TRUE(editorLevel.Set("Trace"));
    ASSERT_TRUE(physicsLevel.Set("0"));
    ASSERT_TRUE(renderLevel.Set("4"));
    ASSERT_TRUE(backendLevel.Set("Debug"));
    ASSERT_TRUE(audioLevel.Set("1"));
    ASSERT_TRUE(ecsLevel.Set("Info"));
    ASSERT_TRUE(networkLevel.Set("invalid"));
    ASSERT_TRUE(namedNetworkLevel.Set("2"));
    ASSERT_TRUE(assetLevel.Set("Warn"));
    ASSERT_TRUE(diagnosticsLevel.Set("Warning"));
    ASSERT_TRUE(maxFileSize.Set("2048"));
    ASSERT_TRUE(retainedFiles.Set("not-a-number"));

    LoggerSession session;
    const auto configuredBreadcrumb =
        session.GetConfiguration().jsonlPath.parent_path() / "override-crash.txt";
    ASSERT_TRUE(breadcrumbs.Set(configuredBreadcrumb.string()));
    ASSERT_TRUE(session.Start());

    EXPECT_EQ(Engine::Log::GetMinimumLevel(), Engine::Log::Level::Warning);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Core), Engine::Log::Level::Debug);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Editor), Engine::Log::Level::Trace);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Physics),
              Engine::Log::Level::Trace);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Render), Engine::Log::Level::Error);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Backend),
              Engine::Log::Level::Debug);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Audio), Engine::Log::Level::Debug);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::ECS), Engine::Log::Level::Info);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Net), Engine::Log::Level::Info);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Network), Engine::Log::Level::Info);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Asset),
              Engine::Log::Level::Warning);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Diagnostics),
              Engine::Log::Level::Warning);
    EXPECT_EQ(Engine::Log::GetHealthSnapshot().breadcrumbPath, configuredBreadcrumb);
}

TEST(LogEngineTests, FormatsAllArgumentKindsAndHandlesMalformedSpecifications)
{
    LoggerSession session;
    ASSERT_TRUE(session.Start());

    Engine::Log::LogEvent formatted =
        MakeEvent(Engine::Log::Level::Info, Engine::Log::Category::Core, "{{value}} {} {} {} {} {}");
    formatted.argumentCount = 5;
    formatted.arguments[0] = Engine::Log::Detail::MakeArgument(-7);
    formatted.arguments[1] = Engine::Log::Detail::MakeArgument(9U);
    formatted.arguments[2] = Engine::Log::Detail::MakeArgument(1.5);
    formatted.arguments[3] = Engine::Log::Detail::MakeArgument(true);
    formatted.arguments[4] = Engine::Log::Detail::MakeArgument("hello");
    ASSERT_TRUE(Engine::Log::Enqueue(formatted));

    Engine::Log::LogEvent badSpecification =
        MakeEvent(Engine::Log::Level::Info, Engine::Log::Category::Core, "value={:invalid}");
    badSpecification.argumentCount = 1;
    badSpecification.arguments[0] = Engine::Log::Detail::MakeArgument(1);
    ASSERT_TRUE(Engine::Log::Enqueue(badSpecification));

    Engine::Log::LogEvent unclosed =
        MakeEvent(Engine::Log::Level::Info, Engine::Log::Category::Core, "dangling { and }");
    ASSERT_TRUE(Engine::Log::Enqueue(unclosed));
    Engine::Log::Flush();

    const auto records = Engine::Log::GetRecentRecords();
    ASSERT_EQ(records.size(), 3U);
    EXPECT_EQ(records[0].text, "{value} -7 9 1.5 true hello");
    EXPECT_EQ(records[1].text, "value=<format-error>");
    EXPECT_EQ(records[2].text, "dangling { and }");
}

TEST(LogEngineTests, AppliesGlobalAndCategoryThresholdsAndReportsInvalidValues)
{
    LoggerSession session;
    ASSERT_TRUE(session.Start());

    Engine::Log::SetMinimumLevel(Engine::Log::Level::Warning);
    EXPECT_EQ(Engine::Log::GetMinimumLevel(), Engine::Log::Level::Warning);
    Engine::Log::SetCategoryMinimumLevel(Engine::Log::Category::Asset, Engine::Log::Level::Error);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Asset), Engine::Log::Level::Error);
    EXPECT_EQ(Engine::Log::GetCategoryMinimumLevel(static_cast<Engine::Log::Category>(255)),
              Engine::Log::Level::Warning);
    Engine::Log::SetCategoryMinimumLevel(static_cast<Engine::Log::Category>(255), Engine::Log::Level::Fatal);

    EXPECT_FALSE(Engine::Log::Enqueue(
        MakeEvent(Engine::Log::Level::Info, Engine::Log::Category::Core, "filtered core info")));
    EXPECT_FALSE(Engine::Log::Enqueue(
        MakeEvent(Engine::Log::Level::Warning, Engine::Log::Category::Asset, "filtered asset warning")));
    EXPECT_TRUE(Engine::Log::Enqueue(
        MakeEvent(Engine::Log::Level::Warning, Engine::Log::Category::Core, "accepted core warning")));
    EXPECT_TRUE(Engine::Log::Enqueue(
        MakeEvent(Engine::Log::Level::Error, Engine::Log::Category::Asset, "accepted asset error")));
    Engine::Log::Flush();

    const auto recent = Engine::Log::GetRecentRecords();
    ASSERT_EQ(recent.size(), 2U);
    EXPECT_EQ(recent[0].text, "accepted core warning");
    EXPECT_EQ(recent[1].text, "accepted asset error");
    EXPECT_EQ(Engine::Log::CategoryFromName("Network"), Engine::Log::Category::Network);
    EXPECT_EQ(Engine::Log::CategoryFromName("unknown"), Engine::Log::Category::Core);
    EXPECT_EQ(std::string(Engine::Log::ToString(static_cast<Engine::Log::Level>(255))), "None");
    EXPECT_EQ(std::string(Engine::Log::ToString(static_cast<Engine::Log::Category>(255))), "Core");
}

TEST(LogEngineTests, RotatesFilesAndCountsBreadcrumbOverwritesAndCallbackFailures)
{
    LoggerSession session(2048, 256);
    ASSERT_TRUE(session.Start());

    const auto callbackToken = Engine::Log::SubscribeCallback(
        [](const Engine::Log::LogRecord &) { throw std::runtime_error("test callback failure"); });
    ASSERT_NE(callbackToken, 0U);

    for (size_t index = 0; index < 1100; ++index)
    {
        Engine::Log::LogEvent event = MakeEvent(Engine::Log::Level::Info, Engine::Log::Category::Core,
                                                "rotation and breadcrumb event {}");
        event.argumentCount = 1;
        event.arguments[0] = Engine::Log::Detail::MakeArgument(index);
        ASSERT_TRUE(Engine::Log::Enqueue(event));
    }
    Engine::Log::Flush();
    for (size_t attempt = 0; attempt < 200; ++attempt)
    {
        if (Engine::Log::GetHealthSnapshot().callbackFailedEvents != 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const Engine::Log::HealthSnapshot health = Engine::Log::GetHealthSnapshot();
    EXPECT_GT(health.breadcrumbOverwrittenEvents, 0U);
    EXPECT_GT(health.callbackFailedEvents, 0U);
    EXPECT_TRUE(std::filesystem::exists(session.GetConfiguration().jsonlPath.string() + ".1"));
    EXPECT_EQ(Engine::Log::GetRecentRecords().size(), 1000U);
    Engine::Log::UnsubscribeCallback(callbackToken);
}
} // namespace
