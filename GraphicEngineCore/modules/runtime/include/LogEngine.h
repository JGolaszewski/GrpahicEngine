#pragma once

#include "runtime/include/LogCommon.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace Engine::Log
{
struct LogRecord
{
    LogEvent event{};
    std::string text;
};

using LogCallback = std::function<void(const LogRecord &)>;

struct Configuration
{
    std::filesystem::path jsonlPath;
    std::filesystem::path breadcrumbPath;
    size_t queueCapacity = 1024;
    uint64_t maxFileSizeBytes = 16 * 1024 * 1024;
    size_t retainedFileCount = 3;
    Level minimumLevel = Level::Trace;
    std::array<Level, CategoryCount> categoryMinimumLevels{};
};

enum class FileSinkStatus : uint8_t
{
    Disabled,
    Opening,
    Ready,
    Failed
};

struct HealthSnapshot
{
    std::string sessionId;
    std::filesystem::path jsonlPath;
    std::filesystem::path breadcrumbPath;
    FileSinkStatus fileStatus = FileSinkStatus::Disabled;
    std::string lastSinkError;
    uint64_t acceptedEvents = 0;
    uint64_t writtenEvents = 0;
    uint64_t queueDroppedEvents = 0;
    uint64_t callbackFailedEvents = 0;
    uint64_t callbackDroppedEvents = 0;
    uint64_t fileFailedEvents = 0;
    uint64_t breadcrumbOverwrittenEvents = 0;
    size_t queueDepth = 0;
    size_t queueCapacity = 0;
    size_t queueHighWaterMark = 0;
};

using CallbackToken = uint64_t;

ENGINE_API bool Initialize(const Configuration &configuration = {});
ENGINE_API void Shutdown() noexcept;
ENGINE_API void Flush() noexcept;
ENGINE_API bool IsInitialized() noexcept;
ENGINE_API void SetFrameIndex(uint64_t frameIndex) noexcept;
ENGINE_API uint64_t GetCurrentOperationId() noexcept;
ENGINE_API void SetCurrentOperationId(uint64_t operationId) noexcept;
ENGINE_API uint64_t CreateOperationId() noexcept;
ENGINE_API CallbackToken SubscribeCallback(LogCallback callback);
ENGINE_API void UnsubscribeCallback(CallbackToken token) noexcept;
ENGINE_API void SetCallback(LogCallback callback);
ENGINE_API void SetMinimumLevel(Level level) noexcept;
ENGINE_API Level GetMinimumLevel() noexcept;
ENGINE_API void SetCategoryMinimumLevel(Category category, Level level) noexcept;
ENGINE_API Level GetCategoryMinimumLevel(Category category) noexcept;
ENGINE_API HealthSnapshot GetHealthSnapshot();
ENGINE_API std::vector<LogRecord> GetRecentRecords();
ENGINE_API void DumpCrashBreadcrumbs(const std::filesystem::path &path) noexcept;
ENGINE_API bool Enqueue(const LogEvent &event) noexcept;
ENGINE_API bool SubmitStructuredEvent(const LogEvent &event) noexcept;

class ScopedOperation
{
  public:
    ScopedOperation(const ScopedOperation &) = delete;
    ScopedOperation &operator=(const ScopedOperation &) = delete;
#if defined(ENGINE_ENABLE_LOGGING) && !ENGINE_ENABLE_LOGGING
    ScopedOperation() noexcept = default;
    explicit ScopedOperation(uint64_t) noexcept {}
    ~ScopedOperation() = default;
#else
    ScopedOperation() noexcept : m_Previous(GetCurrentOperationId())
    {
        SetCurrentOperationId(m_Previous == 0 ? CreateOperationId() : m_Previous);
    }
    explicit ScopedOperation(uint64_t operationId) noexcept : m_Previous(GetCurrentOperationId())
    {
        SetCurrentOperationId(operationId);
    }
    ~ScopedOperation()
    {
        SetCurrentOperationId(m_Previous);
    }
  private:
    uint64_t m_Previous;
#endif
};

namespace Detail
{
template <typename T>
inline LogArgument MakeArgument(T &&input) noexcept
{
    using ValueType = std::remove_cvref_t<T>;
    LogArgument result;
    if constexpr (std::is_same_v<ValueType, bool>)
    {
        result.type = ArgumentType::Boolean;
        result.value.booleanValue = input;
    }
    else if constexpr (std::is_enum_v<ValueType>)
    {
        using Underlying = std::underlying_type_t<ValueType>;
        return MakeArgument(static_cast<Underlying>(input));
    }
    else if constexpr (std::is_integral_v<ValueType> && std::is_signed_v<ValueType>)
    {
        result.type = ArgumentType::Signed;
        result.value.signedValue = static_cast<int64_t>(input);
    }
    else if constexpr (std::is_integral_v<ValueType>)
    {
        result.type = ArgumentType::Unsigned;
        result.value.unsignedValue = static_cast<uint64_t>(input);
    }
    else if constexpr (std::is_floating_point_v<ValueType>)
    {
        result.type = ArgumentType::Floating;
        result.value.floatingValue = static_cast<double>(input);
    }
    else if constexpr (std::convertible_to<T, std::string_view>)
    {
        result.type = ArgumentType::Text;
        const std::string_view view(input);
        const size_t count = (std::min)(view.size(), MaxLogArgumentText - 1);
        if (count > 0)
            std::copy_n(view.data(), count, result.value.text);
        result.value.text[count] = '\0';
    }
    else
    {
        static_assert(sizeof(T) == 0, "Log arguments must be primitive, enum, or string-like values");
    }
    return result;
}

inline void CopyText(char *destination, size_t capacity, std::string_view source) noexcept
{
    if (capacity == 0)
        return;
    const size_t count = (std::min)(source.size(), capacity - 1);
    if (count > 0)
        std::copy_n(source.data(), count, destination);
    destination[count] = '\0';
}
} // namespace Detail

template <typename T>
inline PayloadEntry MakePayloadEntry(std::string_view key, T &&value) noexcept
{
    PayloadEntry entry;
    Detail::CopyText(entry.key, sizeof(entry.key), key);
    entry.value = Detail::MakeArgument(std::forward<T>(value));
    return entry;
}

template <typename... Args>
inline void Submit(Level level, Category category, const char *format,
                   std::source_location location, Args &&...args) noexcept
{
    static_assert(sizeof...(Args) <= MaxLogArguments, "At most eight log arguments are supported");
    LogEvent event;
    event.level = level;
    event.category = category;
    event.format = format;
    event.file = location.file_name();
    event.function = location.function_name();
    event.line = location.line();
    event.argumentCount = static_cast<uint8_t>(sizeof...(Args));
    const std::array<LogArgument, sizeof...(Args)> arguments{Detail::MakeArgument(std::forward<Args>(args))...};
    std::copy(arguments.begin(), arguments.end(), event.arguments);
    (void)Enqueue(event);
}

template <typename... Args>
inline bool SubmitStructured(Level level, Category category, const char *format,
                             std::string_view errorCode, const std::source_location &location,
                             const PayloadEntry *payload, size_t payloadCount, Args &&...args) noexcept
{
    static_assert(sizeof...(Args) <= MaxLogArguments, "At most eight log arguments are supported");
    LogEvent event;
    event.level = level;
    event.category = category;
    event.format = format;
    event.file = location.file_name();
    event.function = location.function_name();
    event.line = location.line();
    Detail::CopyText(event.errorCode, sizeof(event.errorCode), errorCode);
    if (payload != nullptr)
    {
        const size_t safePayloadCount = (std::min)(payloadCount, MaxLogPayloadEntries);
        std::copy_n(payload, safePayloadCount, event.payload);
        event.payloadCount = static_cast<uint8_t>(safePayloadCount);
    }
    event.argumentCount = static_cast<uint8_t>(sizeof...(Args));
    const std::array<LogArgument, sizeof...(Args)> arguments{Detail::MakeArgument(std::forward<Args>(args))...};
    std::copy(arguments.begin(), arguments.end(), event.arguments);
    return SubmitStructuredEvent(event);
}
} // namespace Engine::Log
