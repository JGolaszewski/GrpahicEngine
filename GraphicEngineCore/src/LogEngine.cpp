#include "LogEngine.h"

#include <nlohmann/json.hpp>

#include <array>
#include <bit>
#include <condition_variable>
#include <fstream>
#include <format>
#include <mutex>
#include <sstream>
#include <variant>

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

std::string ArgumentText(const LogArgument &argument, std::string_view specification)
{
    const std::string pattern = "{" + std::string(specification) + "}";
    const auto format = [&pattern](const auto &value) {
        return std::vformat(pattern, std::make_format_args(value));
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
        else if (format[i] == '{' && argumentIndex < event.argumentCount)
        {
            const size_t close = format.find('}', i + 1);
            if (close != std::string_view::npos)
            {
                std::string_view specification;
                const size_t colon = format.find(':', i + 1);
                if (colon != std::string_view::npos && colon < close)
                    specification = format.substr(colon + 1, close - colon - 1);
                result += ArgumentText(event.arguments[argumentIndex++], specification);
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
          mask(capacity - 1)
    {
        // Bounded MPMC sequence queues require a power-of-two capacity.
        capacity = std::bit_ceil(capacity);
        mask = capacity - 1;
        queue = std::make_unique<QueueCell[]>(capacity);
        for (size_t i = 0; i < capacity; ++i)
            queue[i].sequence.store(i, std::memory_order_relaxed);
    }

    Configuration configuration;
    size_t capacity;
    size_t mask;
    std::unique_ptr<QueueCell[]> queue;
    std::atomic<size_t> enqueuePosition{0};
    std::atomic<size_t> dequeuePosition{0};
    std::atomic<size_t> pending{0};
    std::atomic<bool> stopping{false};
    std::thread worker;
    std::mutex wakeMutex;
    std::condition_variable wake;
    std::mutex sinkMutex;
    std::mutex ioMutex;
    std::ofstream jsonl;
    std::array<LogRecord, BreadcrumbCount> breadcrumbs{};
    size_t breadcrumbWrite = 0;
    size_t breadcrumbSize = 0;
    LogCallback callback;

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
                if (enqueuePosition.compare_exchange_weak(position, position + 1,
                                                          std::memory_order_relaxed))
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
                if (dequeuePosition.compare_exchange_weak(position, position + 1,
                                                          std::memory_order_relaxed))
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
                       {"frame_index", event.frameIndex},
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
                jsonl << object.dump() << '\n';
                if (event.level >= Level::Error)
                    jsonl.flush();
            }
        }

        LogCallback activeCallback;
        {
            std::lock_guard lock(sinkMutex);
            breadcrumbs[breadcrumbWrite] = record;
            breadcrumbWrite = (breadcrumbWrite + 1) % BreadcrumbCount;
            breadcrumbSize = (std::min)(breadcrumbSize + 1, BreadcrumbCount);
            activeCallback = callback;
        }
        if (activeCallback)
        {
            try
            {
                activeCallback(record);
            }
            catch (...)
            {
            }
        }
    }

    void Run()
    {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
        if (!configuration.jsonlPath.empty())
        {
            std::error_code error;
            if (configuration.jsonlPath.has_parent_path())
                std::filesystem::create_directories(configuration.jsonlPath.parent_path(), error);
            jsonl.open(configuration.jsonlPath, std::ios::out | std::ios::app);
        }
        for (;;)
        {
            LogEvent event;
            if (Pop(event))
            {
                Process(event);
                pending.fetch_sub(1, std::memory_order_release);
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
        if (jsonl.is_open())
            jsonl.flush();
    }
};

std::atomic<Manager *> gManager{nullptr};
std::mutex gLifecycleMutex;
std::atomic<uint64_t> gFrameIndex{0};
} // namespace

const char *ToString(Level level) noexcept
{
    switch (level)
    {
    case Level::Trace: return "Trace";
    case Level::Debug: return "Debug";
    case Level::Info: return "Info";
    case Level::Warning: return "Warning";
    case Level::Error: return "Error";
    case Level::Fatal: return "Fatal";
    default: return "None";
    }
}

const char *ToString(Category category) noexcept
{
    switch (category)
    {
    case Category::Physics: return "Physics";
    case Category::Render: return "Render";
    case Category::AI: return "AI";
    case Category::Audio: return "Audio";
    case Category::Asset: return "Asset";
    case Category::Editor: return "Editor";
    default: return "General";
    }
}

bool Initialize(const Configuration &configuration)
{
    std::lock_guard lock(gLifecycleMutex);
    if (gManager.load(std::memory_order_acquire))
        return true;
    auto *manager = new Manager(configuration);
    gManager.store(manager, std::memory_order_release);
    try
    {
        manager->worker = std::thread([manager] { manager->Run(); });
    }
    catch (...)
    {
        gManager.store(nullptr, std::memory_order_release);
        delete manager;
        return false;
    }
    return true;
}

void Shutdown() noexcept
{
    std::lock_guard lock(gLifecycleMutex);
    Manager *manager = gManager.exchange(nullptr, std::memory_order_acq_rel);
    if (!manager)
        return;
    manager->stopping.store(true, std::memory_order_release);
    manager->wake.notify_one();
    if (manager->worker.joinable())
        manager->worker.join();
    delete manager;
}

void Flush() noexcept
{
    Manager *manager = gManager.load(std::memory_order_acquire);
    if (!manager)
        return;
    while (manager->pending.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    std::lock_guard lock(manager->ioMutex);
    if (manager->jsonl.is_open())
        manager->jsonl.flush();
}

bool IsInitialized() noexcept
{
    return gManager.load(std::memory_order_acquire) != nullptr;
}

void SetFrameIndex(uint64_t frameIndex) noexcept
{
    gFrameIndex.store(frameIndex, std::memory_order_relaxed);
}

void SetCallback(LogCallback callback)
{
    if (Manager *manager = gManager.load(std::memory_order_acquire))
    {
        std::lock_guard lock(manager->sinkMutex);
        manager->callback = std::move(callback);
    }
}

std::vector<LogRecord> GetRecentRecords()
{
    if (Manager *manager = gManager.load(std::memory_order_acquire))
    {
        std::lock_guard lock(manager->sinkMutex);
        std::vector<LogRecord> records;
        records.reserve(manager->breadcrumbSize);
        const size_t start = (manager->breadcrumbWrite + BreadcrumbCount - manager->breadcrumbSize) %
                             BreadcrumbCount;
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
    try
    {
        std::lock_guard lock(manager->sinkMutex);
        std::ofstream output(path, std::ios::out | std::ios::trunc);
        const size_t start = (manager->breadcrumbWrite + BreadcrumbCount - manager->breadcrumbSize) %
                             BreadcrumbCount;
        for (size_t i = 0; i < manager->breadcrumbSize; ++i)
        {
            const LogRecord &record = manager->breadcrumbs[(start + i) % BreadcrumbCount];
            output << record.event.timestampNs << " [" << ToString(record.event.level) << "] ["
                   << ToString(record.event.category) << "] " << record.text << '\n';
        }
    }
    catch (...)
    {
    }
}

bool Enqueue(const LogEvent &source) noexcept
{
    Manager *manager = gManager.load(std::memory_order_acquire);
    if (!manager || manager->stopping.load(std::memory_order_relaxed))
        return false;
    LogEvent event = source;
    event.timestampNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    event.frameIndex = gFrameIndex.load(std::memory_order_relaxed);
    event.threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    manager->pending.fetch_add(1, std::memory_order_relaxed);
    if (!manager->Push(event))
    {
        manager->pending.fetch_sub(1, std::memory_order_release);
        return false;
    }
    manager->wake.notify_one();
    return true;
}

bool SubmitStructured(Level level, Category category, const char *format,
                      std::string_view errorCode, const std::source_location &location,
                      const PayloadEntry *payload, size_t payloadCount) noexcept
{
    LogEvent event;
    event.level = level;
    event.category = category;
    event.format = format;
    event.file = location.file_name();
    event.function = location.function_name();
    event.line = location.line();
    Detail::CopyText(event.errorCode, sizeof(event.errorCode), errorCode);
    event.payloadCount = static_cast<uint8_t>((std::min)(payloadCount, MaxLogPayloadEntries));
    for (size_t i = 0; i < event.payloadCount; ++i)
        event.payload[i] = payload[i];
    return Enqueue(event);
}
} // namespace Engine::Log
