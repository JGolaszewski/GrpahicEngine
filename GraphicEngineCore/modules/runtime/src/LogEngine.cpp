#include "runtime/include/LogEngine.h"
#include "runtime/include/EnvironmentVariable.h"

#include <nlohmann/json.hpp>

#include <array>
#include <bit>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <format>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace Engine::Log
{
namespace
{
using Json = nlohmann::json;
constexpr size_t BreadcrumbCount = 1000;

void FallbackDiagnostic(std::string_view message) noexcept
{
    try
    {
        std::string terminated(message);
        terminated.push_back('\n');
#ifdef _WIN32
        OutputDebugStringA(terminated.c_str());
#endif
        std::fwrite(terminated.data(), 1, terminated.size(), stderr);
        std::fflush(stderr);
    }
    catch (...)
    {
        std::fputs("Logger diagnostic failure\n", stderr);
        std::fflush(stderr);
    }
}

std::optional<Level> ParseLevel(const char *value) noexcept
{
    if (!value)
        return std::nullopt;
    const std::string_view text(value);
    if (text == "trace" || text == "Trace" || text == "0")
        return Level::Trace;
    if (text == "debug" || text == "Debug" || text == "1")
        return Level::Debug;
    if (text == "info" || text == "Info" || text == "2")
        return Level::Info;
    if (text == "warn" || text == "warning" || text == "Warn" || text == "Warning" || text == "3")
        return Level::Warning;
    if (text == "error" || text == "Error" || text == "4")
        return Level::Error;
    if (text == "fatal" || text == "Fatal" || text == "5")
        return Level::Fatal;
    if (text == "none" || text == "None" || text == "6")
        return Level::None;
    return std::nullopt;
}

std::filesystem::path DefaultLogPath()
{
#ifdef _WIN32
    if (const auto root = ReadEnvironmentVariable("LOCALAPPDATA"))
        return std::filesystem::path(*root) / "GraphicEngine" / "logs" / "engine.jsonl";
#else
    if (const auto root = ReadEnvironmentVariable("XDG_STATE_HOME"))
        return std::filesystem::path(*root) / "graphic-engine" / "engine.jsonl";
    if (const auto root = ReadEnvironmentVariable("HOME"))
        return std::filesystem::path(*root) / ".local" / "state" / "graphic-engine" / "engine.jsonl";
#endif
    return std::filesystem::current_path() / "engine.jsonl";
}

std::string CreateSessionId()
{
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    return std::to_string(now) + "-" +
           std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

Configuration ResolveConfiguration(Configuration configuration)
{
    if (const auto path = ReadEnvironmentVariable("ENGINE_LOG_PATH"))
        configuration.jsonlPath = *path;
    else if (configuration.jsonlPath.empty())
        configuration.jsonlPath = DefaultLogPath();

    if (configuration.breadcrumbPath.empty() && !configuration.jsonlPath.empty())
        configuration.breadcrumbPath = configuration.jsonlPath.parent_path() / "engine-crash-breadcrumbs.log";
    if (const auto path = ReadEnvironmentVariable("ENGINE_LOG_BREADCRUMB_PATH"))
        configuration.breadcrumbPath = *path;
    if (const auto limit = ReadEnvironmentVariable("ENGINE_LOG_MAX_FILE_SIZE_BYTES"))
    {
        char *end = nullptr;
        const unsigned long long parsed = std::strtoull(limit->c_str(), &end, 10);
        if (end != limit->c_str() && *end == '\0')
            configuration.maxFileSizeBytes = static_cast<uint64_t>(parsed);
    }
    if (const auto count = ReadEnvironmentVariable("ENGINE_LOG_RETAINED_FILE_COUNT"))
    {
        char *end = nullptr;
        const unsigned long long parsed = std::strtoull(count->c_str(), &end, 10);
        if (end != count->c_str() && *end == '\0')
            configuration.retainedFileCount = static_cast<size_t>((std::min)(parsed, 32ULL));
    }
    if (const auto value = ReadEnvironmentVariable("ENGINE_LOG_LEVEL"); value)
    {
        if (const auto level = ParseLevel(value->c_str()))
            configuration.minimumLevel = *level;
    }
    constexpr std::array<std::pair<Category, const char *>, 12> categoryVariables{
        {{Category::Core, "ENGINE_LOG_CORE_LEVEL"},
         {Category::Editor, "ENGINE_LOG_EDITOR_LEVEL"},
         {Category::Physics, "ENGINE_LOG_PHYSICS_LEVEL"},
         {Category::Render, "ENGINE_LOG_RENDER_LEVEL"},
         {Category::Backend, "ENGINE_LOG_BACKEND_LEVEL"},
         {Category::Audio, "ENGINE_LOG_AUDIO_LEVEL"},
         {Category::ECS, "ENGINE_LOG_ECS_LEVEL"},
         {Category::Net, "ENGINE_LOG_NET_LEVEL"},
         {Category::Network, "ENGINE_LOG_NETWORK_LEVEL"},
         {Category::Asset, "ENGINE_LOG_ASSET_LEVEL"},
         {Category::Diagnostics, "ENGINE_LOG_DIAGNOSTICS_LEVEL"},
         {Category::Count, nullptr}}};
    for (const auto &[category, variable] : categoryVariables)
    {
        if (!variable)
            continue;
        if (const auto value = ReadEnvironmentVariable(variable); value)
        {
            if (const auto level = ParseLevel(value->c_str()))
                configuration.categoryMinimumLevels[static_cast<size_t>(category)] = *level;
        }
    }
    return configuration;
}

std::string ArgumentText(const LogArgument &argument, std::string_view specification)
{
    const std::string pattern =
        "{" + (specification.empty() ? std::string{} : ":" + std::string(specification)) + "}";
    const auto format = [&pattern](const auto &value) {
        try
        {
            return std::vformat(pattern, std::make_format_args(value));
        }
        catch (const std::format_error &)
        {
            return std::string("<format-error>");
        }
    };
    switch (argument.type)
    {
    case ArgumentType::Signed:
        return format(argument.value.signedValue);
    case ArgumentType::Unsigned:
        return format(argument.value.unsignedValue);
    case ArgumentType::Floating:
        return format(argument.value.floatingValue);
    case ArgumentType::Boolean:
        return format(argument.value.booleanValue);
    case ArgumentType::Text:
        return format(argument.value.text);
    }
    return {};
}

std::string FormatMessage(const LogEvent &event)
{
    std::string result;
    const std::string_view format = event.format ? event.format : "";
    result.reserve(format.size() + static_cast<size_t>(event.argumentCount) * 16);
    size_t argumentIndex = 0;
    for (size_t i = 0; i < format.size(); ++i)
    {
        if (format[i] == '{' && i + 1 < format.size() && format[i + 1] == '{')
        {
            result.push_back('{');
            ++i;
        }
        else if (format[i] == '}' && i + 1 < format.size() && format[i + 1] == '}')
        {
            result.push_back('}');
            ++i;
        }
        else if (format[i] == '{' && argumentIndex < MaxLogArguments && argumentIndex < event.argumentCount)
        {
            const size_t close = format.find('}', i + 1);
            if (close != std::string_view::npos)
            {
                std::string_view specification;
                const size_t colon = format.find(':', i + 1);
                if (colon != std::string_view::npos && colon < close)
                    specification = format.substr(colon + 1, close - colon - 1);
                result += ArgumentText(event.arguments.at(argumentIndex++), specification);
                i = close;
            }
            else
                result.push_back(format[i]);
        }
        else
            result.push_back(format[i]);
    }
    return result;
}

void FallbackEvent(const LogEvent &event) noexcept
{
    if (event.level < Level::Error)
        return;
    try
    {
        std::string diagnostic = "Critical log event could not be queued [";
        diagnostic += ToString(event.level);
        diagnostic += "][";
        diagnostic += ToString(event.category);
        diagnostic += ']';
        if (event.errorCode[0])
        {
            diagnostic += " [";
            diagnostic += event.errorCode;
            diagnostic += ']';
        }
        diagnostic += ": ";
        diagnostic += FormatMessage(event);
        FallbackDiagnostic(diagnostic);
    }
    catch (...)
    {
        FallbackDiagnostic("Critical log event could not be queued");
    }
}

Json ArgumentJson(const LogArgument &argument)
{
    switch (argument.type)
    {
    case ArgumentType::Signed:
        return argument.value.signedValue;
    case ArgumentType::Unsigned:
        return argument.value.unsignedValue;
    case ArgumentType::Floating:
        return argument.value.floatingValue;
    case ArgumentType::Boolean:
        return argument.value.booleanValue;
    case ArgumentType::Text:
        return argument.value.text;
    }
    return nullptr;
}

struct QueueCell
{
    std::atomic<size_t> sequence{0};
    LogEvent event{};
};

struct Manager
{
    explicit Manager(Configuration config)
        : configuration(std::move(config)), capacity((std::max)(size_t{2}, configuration.queueCapacity)),
          mask(capacity - 1), minimumLevel(static_cast<uint8_t>(configuration.minimumLevel))
    {
        // Bounded MPMC sequence queues require a power-of-two capacity.
        capacity = std::bit_ceil(capacity);
        mask = capacity - 1;
        queue = std::make_unique<QueueCell[]>(capacity);
        for (size_t i = 0; i < capacity; ++i)
            queue[i].sequence.store(i, std::memory_order_relaxed);
        for (size_t i = 0; i < CategoryCount; ++i)
            categoryMinimumLevels[i].store(static_cast<uint8_t>(configuration.categoryMinimumLevels[i]),
                                           std::memory_order_relaxed);
    }

    Configuration configuration;
    std::string sessionId = CreateSessionId();
    size_t capacity;
    size_t mask;
    std::unique_ptr<QueueCell[]> queue;
    std::atomic<size_t> enqueuePosition{0};
    std::atomic<size_t> dequeuePosition{0};
    std::atomic<size_t> pending{0};
    std::atomic<size_t> queueHighWaterMark{0};
    std::atomic<uint64_t> acceptedEvents{0};
    std::atomic<uint64_t> writtenEvents{0};
    std::atomic<uint64_t> queueDroppedEvents{0};
    std::atomic<uint64_t> callbackFailedEvents{0};
    std::atomic<uint64_t> callbackDroppedEvents{0};
    std::atomic<uint64_t> fileFailedEvents{0};
    std::atomic<uint64_t> breadcrumbOverwrittenEvents{0};
    std::atomic<uint8_t> minimumLevel;
    std::array<std::atomic<uint8_t>, CategoryCount> categoryMinimumLevels{};
    std::atomic<bool> stopping{false};
    std::thread worker;
    std::thread callbackWorker;
    std::mutex wakeMutex;
    std::condition_variable wake;
    std::mutex drainMutex;
    std::condition_variable drained;
    std::mutex callbackQueueMutex;
    std::condition_variable callbackWake;
    std::deque<std::pair<LogRecord, std::vector<CallbackToken>>> callbackQueue;
    bool callbackStopping = false;
    std::mutex startupMutex;
    std::condition_variable startupReady;
    bool startupComplete = false;
    std::mutex sinkMutex;
    std::mutex ioMutex;
    std::mutex sinkErrorMutex;
    std::ofstream jsonl;
    std::atomic<FileSinkStatus> fileStatus{FileSinkStatus::Disabled};
    std::string lastSinkError;
    uint64_t jsonlBytes = 0;
    std::array<LogRecord, BreadcrumbCount> breadcrumbs{};
    size_t breadcrumbWrite = 0;
    size_t breadcrumbSize = 0;
    std::unordered_map<CallbackToken, LogCallback> callbacks;

    bool Accepts(const LogEvent &event) const noexcept
    {
        const auto level = static_cast<uint8_t>(event.level);
        const size_t category = static_cast<size_t>(event.category);
        const uint8_t globalMinimum = minimumLevel.load(std::memory_order_relaxed);
        const uint8_t categoryMinimum = category < CategoryCount
                                            ? categoryMinimumLevels[category].load(std::memory_order_relaxed)
                                            : globalMinimum;
        return level >= (std::max)(globalMinimum, categoryMinimum);
    }

    void SetSinkFailure(const std::string &message)
    {
        fileStatus.store(FileSinkStatus::Failed, std::memory_order_release);
        fileFailedEvents.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lock(sinkErrorMutex);
            lastSinkError = message;
        }
        FallbackDiagnostic(message);
    }

    bool RotateFile()
    {
        if (!configuration.maxFileSizeBytes || !jsonlBytes || jsonlBytes < configuration.maxFileSizeBytes)
            return true;
        jsonl.flush();
        jsonl.close();
        std::error_code error;
        if (configuration.retainedFileCount == 0)
        {
            std::filesystem::remove(configuration.jsonlPath, error);
        }
        else
        {
            const std::filesystem::path oldest =
                configuration.jsonlPath.string() + "." + std::to_string(configuration.retainedFileCount);
            std::filesystem::remove(oldest, error);
            error.clear();
            for (size_t index = configuration.retainedFileCount; index > 1; --index)
            {
                const auto from =
                    std::filesystem::path(configuration.jsonlPath.string() + "." + std::to_string(index - 1));
                const auto to =
                    std::filesystem::path(configuration.jsonlPath.string() + "." + std::to_string(index));
                if (std::filesystem::exists(from, error))
                {
                    std::filesystem::rename(from, to, error);
                    if (error)
                        break;
                }
                error.clear();
            }
            if (!error)
                std::filesystem::rename(configuration.jsonlPath, configuration.jsonlPath.string() + ".1",
                                        error);
        }
        if (error)
        {
            SetSinkFailure("Log file rotation failed: " + error.message());
            return false;
        }
        jsonl.open(configuration.jsonlPath, std::ios::out | std::ios::trunc);
        if (!jsonl)
        {
            SetSinkFailure("Log file reopen after rotation failed: " + configuration.jsonlPath.string());
            return false;
        }
        jsonlBytes = 0;
        fileStatus.store(FileSinkStatus::Ready, std::memory_order_release);
        return true;
    }

    bool Push(const LogEvent &event) noexcept
    {
        size_t position = enqueuePosition.load(std::memory_order_relaxed);
        QueueCell *cell;
        for (;;)
        {
            cell = &queue[position & mask];
            const size_t sequence = cell->sequence.load(std::memory_order_acquire);
            const intptr_t difference = static_cast<intptr_t>(sequence) - static_cast<intptr_t>(position);
            if (difference == 0)
            {
                if (enqueuePosition.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                    break;
            }
            else if (difference < 0)
                return false;
            else
                position = enqueuePosition.load(std::memory_order_relaxed);
        }
        cell->event = event;
        cell->sequence.store(position + 1, std::memory_order_release);
        return true;
    }

    bool Pop(LogEvent &event) noexcept
    {
        size_t position = dequeuePosition.load(std::memory_order_relaxed);
        QueueCell *cell;
        for (;;)
        {
            cell = &queue[position & mask];
            const size_t sequence = cell->sequence.load(std::memory_order_acquire);
            const intptr_t difference = static_cast<intptr_t>(sequence) - static_cast<intptr_t>(position + 1);
            if (difference == 0)
            {
                if (dequeuePosition.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                    break;
            }
            else if (difference < 0)
                return false;
            else
                position = dequeuePosition.load(std::memory_order_relaxed);
        }
        event = cell->event;
        cell->sequence.store(position + capacity, std::memory_order_release);
        return true;
    }

    void Process(const LogEvent &event)
    {
        LogRecord record{event, FormatMessage(event)};
        Json payload = Json::object();
        for (size_t i = 0; i < event.payloadCount; ++i)
            payload[event.payload[i].key] = ArgumentJson(event.payload[i].value);
        Json arguments = Json::array();
        for (size_t i = 0; i < event.argumentCount; ++i)
            arguments.push_back(ArgumentJson(event.arguments[i]));

        Json object = {{"timestamp_ns", event.timestampNs},
                       {"monotonic_ns", event.monotonicNs},
                       {"frame_index", event.frameIndex},
                       {"operation_id", event.operationId},
                       {"session_id", sessionId},
                       {"level", ToString(event.level)},
                       {"category", ToString(event.category)},
                       {"file", event.file ? event.file : ""},
                       {"line", event.line},
                       {"function", event.function ? event.function : ""},
                       {"error_code", event.errorCode[0] ? Json(event.errorCode) : Json(nullptr)},
                       {"message", record.text},
                       {"payload", std::move(payload)},
                       {"arguments", std::move(arguments)},
                       {"thread_id", event.threadId}};
        {
            std::lock_guard lock(ioMutex);
            if (jsonl.is_open())
            {
                if (RotateFile() && jsonl.is_open())
                {
                    const std::string line = object.dump() + '\n';
                    jsonl.write(line.data(), static_cast<std::streamsize>(line.size()));
                    if (!jsonl)
                    {
                        jsonl.close();
                        SetSinkFailure("Log file write failed: " + configuration.jsonlPath.string());
                    }
                    else
                    {
                        jsonlBytes += line.size();
                        writtenEvents.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (event.level >= Level::Error)
                    {
                        jsonl.flush();
                        if (!jsonl)
                        {
                            jsonl.close();
                            SetSinkFailure("Log file flush failed: " + configuration.jsonlPath.string());
                        }
                    }
                }
            }
        }

        std::vector<CallbackToken> activeCallbacks;
        {
            std::lock_guard lock(sinkMutex);
            if (breadcrumbSize == BreadcrumbCount)
                breadcrumbOverwrittenEvents.fetch_add(1, std::memory_order_relaxed);
            breadcrumbs[breadcrumbWrite] = record;
            breadcrumbWrite = (breadcrumbWrite + 1) % BreadcrumbCount;
            breadcrumbSize = (std::min)(breadcrumbSize + 1, BreadcrumbCount);
            activeCallbacks.reserve(callbacks.size());
            for (const auto &[token, callback] : callbacks)
            {
                (void)token;
                activeCallbacks.push_back(token);
            }
        }
        if (!activeCallbacks.empty())
        {
            bool queueFull = false;
            try
            {
                std::lock_guard lock(callbackQueueMutex);
                if (callbackQueue.size() < capacity)
                {
                    callbackQueue.emplace_back(record, std::move(activeCallbacks));
                    callbackWake.notify_one();
                }
                else
                    queueFull = true;
            }
            catch (...)
            {
                queueFull = true;
            }
            if (queueFull)
                callbackDroppedEvents.fetch_add(1, std::memory_order_relaxed);
        }
        if (event.level == Level::Fatal && !configuration.breadcrumbPath.empty())
            WriteBreadcrumbs(configuration.breadcrumbPath);
    }

    void WriteBreadcrumbs(const std::filesystem::path &path) noexcept
    {
        try
        {
            std::lock_guard lock(sinkMutex);
            std::error_code error;
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path(), error);
            if (error)
            {
                FallbackDiagnostic("Unable to create breadcrumb directory: " + error.message());
                return;
            }
            std::ofstream output(path, std::ios::out | std::ios::trunc);
            if (!output)
            {
                FallbackDiagnostic("Unable to persist log breadcrumbs: " + path.string());
                return;
            }
            const size_t start = (breadcrumbWrite + BreadcrumbCount - breadcrumbSize) % BreadcrumbCount;
            for (size_t i = 0; i < breadcrumbSize; ++i)
            {
                const LogRecord &record = breadcrumbs[(start + i) % BreadcrumbCount];
                output << record.event.timestampNs << " [" << ToString(record.event.level) << "]["
                       << ToString(record.event.category) << "][frame=" << record.event.frameIndex
                       << "][operation=" << record.event.operationId << "]";
                if (record.event.errorCode[0])
                    output << "[code=" << record.event.errorCode << "]";
                output << ' ' << (record.event.file ? record.event.file : "") << ':' << record.event.line
                       << ' ' << record.text << '\n';
            }
            output.flush();
            if (!output)
                FallbackDiagnostic("Unable to complete log breadcrumb write: " + path.string());
        }
        catch (...)
        {
            FallbackDiagnostic("Unexpected error while persisting log breadcrumbs");
        }
    }

    void RunCallbacks()
    {
        for (;;)
        {
            std::pair<LogRecord, std::vector<CallbackToken>> work;
            {
                std::unique_lock lock(callbackQueueMutex);
                callbackWake.wait(lock, [this] { return callbackStopping || !callbackQueue.empty(); });
                if (callbackQueue.empty() && callbackStopping)
                    break;
                work = std::move(callbackQueue.front());
                callbackQueue.pop_front();
            }
            for (CallbackToken token : work.second)
            {
                LogCallback callback;
                {
                    std::lock_guard lock(sinkMutex);
                    const auto found = callbacks.find(token);
                    if (found != callbacks.end())
                        callback = found->second;
                }
                if (!callback)
                    continue;
                try
                {
                    callback(work.first);
                }
                catch (...)
                {
                    callbackFailedEvents.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    }

    void Run()
    {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
        try
        {
            if (!configuration.jsonlPath.empty())
            {
                fileStatus.store(FileSinkStatus::Opening, std::memory_order_release);
                std::error_code error;
                if (configuration.jsonlPath.has_parent_path())
                    std::filesystem::create_directories(configuration.jsonlPath.parent_path(), error);
                if (error)
                    SetSinkFailure("Unable to create log directory: " + error.message());
                else
                {
                    jsonl.open(configuration.jsonlPath, std::ios::out | std::ios::app);
                    if (!jsonl)
                        SetSinkFailure("Unable to open log file: " + configuration.jsonlPath.string());
                    else
                    {
                        jsonlBytes = std::filesystem::file_size(configuration.jsonlPath, error);
                        if (error)
                            jsonlBytes = 0;
                        const std::string header =
                            Json{{"type", "session_start"},
                                 {"session_id", sessionId},
                                 {"timestamp_ns", std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                      std::chrono::system_clock::now().time_since_epoch())
                                                      .count()}}
                                .dump() +
                            '\n';
                        jsonl.write(header.data(), static_cast<std::streamsize>(header.size()));
                        if (!jsonl)
                        {
                            jsonl.close();
                            SetSinkFailure("Unable to write the log session header: " +
                                           configuration.jsonlPath.string());
                        }
                        else
                        {
                            jsonlBytes += header.size();
                            fileStatus.store(FileSinkStatus::Ready, std::memory_order_release);
                        }
                    }
                }
            }
            else
                fileStatus.store(FileSinkStatus::Disabled, std::memory_order_release);
        }
        catch (...)
        {
            if (jsonl.is_open())
                jsonl.close();
            SetSinkFailure("Unexpected error while opening the log file");
        }
        {
            std::lock_guard lock(startupMutex);
            startupComplete = true;
        }
        startupReady.notify_all();
        for (;;)
        {
            LogEvent event;
            if (Pop(event))
            {
                try
                {
                    Process(event);
                }
                catch (...)
                {
                    FallbackDiagnostic("Unhandled error while processing a log event");
                }
                pending.fetch_sub(1, std::memory_order_release);
                drained.notify_all();
                continue;
            }
            if (stopping.load(std::memory_order_acquire) && pending.load(std::memory_order_acquire) == 0)
                break;
            std::unique_lock lock(wakeMutex);
            wake.wait_for(lock, std::chrono::milliseconds(10), [this] {
                return stopping.load(std::memory_order_relaxed) ||
                       pending.load(std::memory_order_relaxed) != 0;
            });
        }
        {
            std::lock_guard lock(ioMutex);
            if (jsonl.is_open())
            {
                jsonl.flush();
                if (!jsonl)
                {
                    jsonl.close();
                    SetSinkFailure("Log file flush failed during shutdown: " +
                                   configuration.jsonlPath.string());
                }
            }
        }
    }
};

std::atomic<Manager *> gManager{nullptr};
std::mutex gLifecycleMutex;
bool gShuttingDown = false;
std::atomic<uint64_t> gFrameIndex{0};
std::atomic<uint64_t> gNextOperationId{1};
std::atomic<size_t> gActiveProducers{0};
thread_local uint64_t gCurrentOperationId = 0;
std::atomic<uint8_t> gMinimumLevel{static_cast<uint8_t>(Level::Trace)};
std::array<std::atomic<uint8_t>, CategoryCount> gCategoryMinimumLevels{};
std::atomic<CallbackToken> gNextCallbackToken{1};
std::mutex gLegacyCallbackMutex;
CallbackToken gLegacyCallbackToken = 0;
} // namespace

const char *ToString(Level level) noexcept
{
    switch (level)
    {
    case Level::Trace:
        return "Trace";
    case Level::Debug:
        return "Debug";
    case Level::Info:
        return "Info";
    case Level::Warning:
        return "Warning";
    case Level::Error:
        return "Error";
    case Level::Fatal:
        return "Fatal";
    default:
        return "None";
    }
}

const char *ToString(Category category) noexcept
{
    switch (category)
    {
    case Category::Core:
        return "Core";
    case Category::Editor:
        return "Editor";
    case Category::Physics:
        return "Physics";
    case Category::Render:
        return "Render";
    case Category::Backend:
        return "Backend";
    case Category::Audio:
        return "Audio";
    case Category::ECS:
        return "ECS";
    case Category::Net:
        return "Network";
    case Category::Asset:
        return "Asset";
    case Category::Diagnostics:
        return "Diagnostics";
    default:
        return "Core";
    }
}

Category CategoryFromName(std::string_view name) noexcept
{
    if (name == "Render")
        return Category::Render;
    if (name == "Editor")
        return Category::Editor;
    if (name == "Backend")
        return Category::Backend;
    if (name == "Diagnostics")
        return Category::Diagnostics;
    if (name == "Physics")
        return Category::Physics;
    if (name == "Audio")
        return Category::Audio;
    if (name == "ECS")
        return Category::ECS;
    if (name == "Net")
        return Category::Net;
    if (name == "Network")
        return Category::Network;
    if (name == "Asset")
        return Category::Asset;
    return Category::Core;
}

bool Initialize(const Configuration &configuration)
{
    std::lock_guard lock(gLifecycleMutex);
    if (gShuttingDown)
        return false;
    if (gManager.load(std::memory_order_acquire))
        return gManager.load(std::memory_order_relaxed)->fileStatus.load(std::memory_order_acquire) !=
               FileSinkStatus::Failed;
    Manager *manager = nullptr;
    try
    {
        Configuration resolved = ResolveConfiguration(configuration);
        gMinimumLevel.store(static_cast<uint8_t>(resolved.minimumLevel), std::memory_order_relaxed);
        for (size_t i = 0; i < CategoryCount; ++i)
            gCategoryMinimumLevels[i].store(static_cast<uint8_t>(resolved.categoryMinimumLevels[i]),
                                            std::memory_order_relaxed);
        manager = new Manager(std::move(resolved));
    }
    catch (...)
    {
        FallbackDiagnostic("Unable to allocate the logger state");
        return false;
    }
    try
    {
        manager->worker = std::thread([manager] { manager->Run(); });
    }
    catch (...)
    {
        delete manager;
        FallbackDiagnostic("Unable to start the logger worker thread");
        return false;
    }
    try
    {
        manager->callbackWorker = std::thread([manager] { manager->RunCallbacks(); });
    }
    catch (...)
    {
        manager->stopping.store(true, std::memory_order_release);
        manager->wake.notify_one();
        if (manager->worker.joinable())
            manager->worker.join();
        delete manager;
        FallbackDiagnostic("Unable to start the logger callback worker thread");
        return false;
    }
    gManager.store(manager, std::memory_order_release);
    {
        std::unique_lock startupLock(manager->startupMutex);
        manager->startupReady.wait(startupLock, [manager] { return manager->startupComplete; });
    }
    return manager->fileStatus.load(std::memory_order_acquire) != FileSinkStatus::Failed;
}

void Shutdown() noexcept
{
    Manager *manager = nullptr;
    {
        std::lock_guard lock(gLifecycleMutex);
        manager = gManager.exchange(nullptr, std::memory_order_acq_rel);
        if (!manager)
            return;
        gShuttingDown = true;
    }
    while (gActiveProducers.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    manager->stopping.store(true, std::memory_order_release);
    manager->wake.notify_one();
    if (manager->worker.joinable())
        manager->worker.join();
    {
        std::lock_guard callbackLock(manager->callbackQueueMutex);
        manager->callbackStopping = true;
    }
    manager->callbackWake.notify_one();
    if (manager->callbackWorker.joinable())
        manager->callbackWorker.join();
    delete manager;
    {
        std::lock_guard lock(gLifecycleMutex);
        gShuttingDown = false;
    }
}

void Flush() noexcept
{
    try
    {
        Manager *manager = gManager.load(std::memory_order_acquire);
        if (!manager)
            return;
        std::unique_lock drainLock(manager->drainMutex);
        if (!manager->drained.wait_for(drainLock, std::chrono::seconds(5), [manager] {
                return manager->pending.load(std::memory_order_acquire) == 0;
            }))
            FallbackDiagnostic("Timed out while flushing the log queue");
        drainLock.unlock();
        std::lock_guard lock(manager->ioMutex);
        if (manager->jsonl.is_open())
        {
            manager->jsonl.flush();
            if (!manager->jsonl)
            {
                manager->jsonl.close();
                manager->SetSinkFailure("Log file flush failed: " +
                                        manager->configuration.jsonlPath.string());
            }
        }
    }
    catch (...)
    {
        FallbackDiagnostic("Unexpected error while flushing the log queue");
    }
}

bool IsInitialized() noexcept
{
    return gManager.load(std::memory_order_acquire) != nullptr;
}

void SetFrameIndex(uint64_t frameIndex) noexcept
{
    gFrameIndex.store(frameIndex, std::memory_order_relaxed);
}

uint64_t GetCurrentOperationId() noexcept
{
    return gCurrentOperationId;
}

void SetCurrentOperationId(uint64_t operationId) noexcept
{
    gCurrentOperationId = operationId;
}

uint64_t CreateOperationId() noexcept
{
    return gNextOperationId.fetch_add(1, std::memory_order_relaxed);
}

CallbackToken SubscribeCallback(LogCallback callback)
{
    if (!callback)
        return 0;
    const CallbackToken token = gNextCallbackToken.fetch_add(1, std::memory_order_relaxed);
    if (Manager *manager = gManager.load(std::memory_order_acquire))
    {
        std::lock_guard lock(manager->sinkMutex);
        manager->callbacks.emplace(token, std::move(callback));
        return token;
    }
    return 0;
}

void UnsubscribeCallback(CallbackToken token) noexcept
{
    if (token == 0)
        return;
    if (Manager *manager = gManager.load(std::memory_order_acquire))
    {
        std::lock_guard lock(manager->sinkMutex);
        manager->callbacks.erase(token);
    }
}

void SetCallback(LogCallback callback)
{
    std::lock_guard lock(gLegacyCallbackMutex);
    UnsubscribeCallback(gLegacyCallbackToken);
    gLegacyCallbackToken = callback ? SubscribeCallback(std::move(callback)) : 0;
}

void SetMinimumLevel(Level level) noexcept
{
    gMinimumLevel.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
    if (Manager *manager = gManager.load(std::memory_order_acquire))
        manager->minimumLevel.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
}

Level GetMinimumLevel() noexcept
{
    return static_cast<Level>(gMinimumLevel.load(std::memory_order_relaxed));
}

void SetCategoryMinimumLevel(Category category, Level level) noexcept
{
    const size_t index = static_cast<size_t>(category);
    if (index >= CategoryCount)
        return;
    gCategoryMinimumLevels[index].store(static_cast<uint8_t>(level), std::memory_order_relaxed);
    if (Manager *manager = gManager.load(std::memory_order_acquire))
        manager->categoryMinimumLevels[index].store(static_cast<uint8_t>(level), std::memory_order_relaxed);
}

Level GetCategoryMinimumLevel(Category category) noexcept
{
    const size_t index = static_cast<size_t>(category);
    if (index >= CategoryCount)
        return GetMinimumLevel();
    return static_cast<Level>(gCategoryMinimumLevels[index].load(std::memory_order_relaxed));
}

HealthSnapshot GetHealthSnapshot()
{
    HealthSnapshot result;
    if (Manager *manager = gManager.load(std::memory_order_acquire))
    {
        result.jsonlPath = manager->configuration.jsonlPath;
        result.breadcrumbPath = manager->configuration.breadcrumbPath;
        result.sessionId = manager->sessionId;
        result.fileStatus = manager->fileStatus.load(std::memory_order_acquire);
        result.acceptedEvents = manager->acceptedEvents.load(std::memory_order_relaxed);
        result.writtenEvents = manager->writtenEvents.load(std::memory_order_relaxed);
        result.queueDroppedEvents = manager->queueDroppedEvents.load(std::memory_order_relaxed);
        result.callbackFailedEvents = manager->callbackFailedEvents.load(std::memory_order_relaxed);
        result.callbackDroppedEvents = manager->callbackDroppedEvents.load(std::memory_order_relaxed);
        result.fileFailedEvents = manager->fileFailedEvents.load(std::memory_order_relaxed);
        result.breadcrumbOverwrittenEvents =
            manager->breadcrumbOverwrittenEvents.load(std::memory_order_relaxed);
        result.queueDepth = manager->pending.load(std::memory_order_relaxed);
        result.queueCapacity = manager->capacity;
        result.queueHighWaterMark = manager->queueHighWaterMark.load(std::memory_order_relaxed);
        std::lock_guard lock(manager->sinkErrorMutex);
        result.lastSinkError = manager->lastSinkError;
    }
    return result;
}

std::vector<LogRecord> GetRecentRecords()
{
    if (Manager *manager = gManager.load(std::memory_order_acquire))
    {
        std::lock_guard lock(manager->sinkMutex);
        std::vector<LogRecord> records;
        records.reserve(manager->breadcrumbSize);
        const size_t start =
            (manager->breadcrumbWrite + BreadcrumbCount - manager->breadcrumbSize) % BreadcrumbCount;
        for (size_t i = 0; i < manager->breadcrumbSize; ++i)
            records.push_back(manager->breadcrumbs[(start + i) % BreadcrumbCount]);
        return records;
    }
    return {};
}

void DumpCrashBreadcrumbs(const std::filesystem::path &path) noexcept
{
    Manager *manager = gManager.load(std::memory_order_acquire);
    if (!manager)
        return;
    Flush();
    manager->WriteBreadcrumbs(path);
}

bool Enqueue(const LogEvent &source) noexcept
{
    gActiveProducers.fetch_add(1, std::memory_order_acq_rel);
    Manager *manager = gManager.load(std::memory_order_acquire);
    if (!manager || manager->stopping.load(std::memory_order_relaxed))
    {
        FallbackEvent(source);
        gActiveProducers.fetch_sub(1, std::memory_order_release);
        return false;
    }
    LogEvent event = source;
    if (!manager->Accepts(event) && event.level < Level::Error)
    {
        gActiveProducers.fetch_sub(1, std::memory_order_release);
        return false;
    }
    event.timestampNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                  std::chrono::system_clock::now().time_since_epoch())
                                                  .count());
    event.monotonicNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                  std::chrono::steady_clock::now().time_since_epoch())
                                                  .count());
    event.frameIndex = gFrameIndex.load(std::memory_order_relaxed);
    event.threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    if (event.operationId == 0)
        event.operationId = gCurrentOperationId;
    const size_t depth = manager->pending.fetch_add(1, std::memory_order_relaxed) + 1;
    size_t highWater = manager->queueHighWaterMark.load(std::memory_order_relaxed);
    while (depth > highWater &&
           !manager->queueHighWaterMark.compare_exchange_weak(highWater, depth, std::memory_order_relaxed))
    {
    }
    if (!manager->Push(event))
    {
        manager->pending.fetch_sub(1, std::memory_order_release);
        manager->queueDroppedEvents.fetch_add(1, std::memory_order_relaxed);
        FallbackEvent(event);
        manager->drained.notify_all();
        gActiveProducers.fetch_sub(1, std::memory_order_release);
        return false;
    }
    manager->acceptedEvents.fetch_add(1, std::memory_order_relaxed);
    manager->wake.notify_one();
    gActiveProducers.fetch_sub(1, std::memory_order_release);
    return true;
}

bool SubmitStructuredEvent(const LogEvent &event) noexcept
{
    return Enqueue(event);
}
} // namespace Engine::Log
