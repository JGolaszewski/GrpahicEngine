#pragma once

#include "LogCommon.h"

#include <algorithm>
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
    std::filesystem::path jsonlPath = "engine.jsonl";
    size_t queueCapacity = 1024;
};

ENGINE_API bool Initialize(const Configuration &configuration = {});
ENGINE_API void Shutdown() noexcept;
ENGINE_API void Flush() noexcept;
ENGINE_API bool IsInitialized() noexcept;
ENGINE_API void SetFrameIndex(uint64_t frameIndex) noexcept;
ENGINE_API void SetCallback(LogCallback callback);
ENGINE_API std::vector<LogRecord> GetRecentRecords();
ENGINE_API void DumpCrashBreadcrumbs(const std::filesystem::path &path) noexcept;
ENGINE_API bool Enqueue(const LogEvent &event) noexcept;

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
    size_t index = 0;
    ((event.arguments[index++] = Detail::MakeArgument(std::forward<Args>(args))), ...);
    (void)Enqueue(event);
}

ENGINE_API bool SubmitStructured(Level level, Category category, const char *format,
                                 std::string_view errorCode, const std::source_location &location,
                                 const PayloadEntry *payload, size_t payloadCount) noexcept;
} // namespace Engine::Log
