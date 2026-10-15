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

namespace Engine::Log {
/// Event plus formatted text delivered to callbacks and recent-record queries.
struct LogRecord {
  LogEvent event{}; ///< Original structured event metadata and arguments.
  std::string text; ///< Rendered human-readable message.
};

/// Consumer invoked when a log record is dispatched.
using LogCallback = std::function<void(const LogRecord&)>;

/// File and queue settings used when initializing the logging engine.
struct Configuration {
  std::filesystem::path jsonlPath;                          ///< JSONL output path; empty disables the file sink.
  std::filesystem::path breadcrumbPath;                     ///< Crash breadcrumb path; empty disables breadcrumbs.
  size_t queueCapacity = 1024;                              ///< Maximum queued events before new events are dropped.
  uint64_t maxFileSizeBytes = 16 * 1024 * 1024;             ///< Maximum size before rotating the JSONL file.
  size_t retainedFileCount = 3;                             ///< Number of rotated JSONL files to retain.
  Level minimumLevel = Level::Trace;                        ///< Default severity threshold.
  std::array<Level, CategoryCount> categoryMinimumLevels{}; ///< Optional per-category thresholds.
};

/// State of the asynchronous JSONL output sink.
enum class FileSinkStatus : uint8_t { Disabled, Opening, Ready, Failed };

/// Current logging session health and dropped-event counters.
struct HealthSnapshot {
  std::string sessionId;                                ///< Unique identifier for the active logging session.
  std::filesystem::path jsonlPath;                      ///< Active JSONL output path.
  std::filesystem::path breadcrumbPath;                 ///< Active crash breadcrumb path.
  FileSinkStatus fileStatus = FileSinkStatus::Disabled; ///< Current file sink state.
  std::string lastSinkError;                            ///< Most recent file sink diagnostic.
  uint64_t acceptedEvents = 0;                          ///< Events accepted by the producer queue.
  uint64_t writtenEvents = 0;                           ///< Events written to the JSONL sink.
  uint64_t queueDroppedEvents = 0;                      ///< Events dropped because the queue was full.
  uint64_t callbackFailedEvents = 0;                    ///< Callback invocations that threw an exception.
  uint64_t callbackDroppedEvents = 0;                   ///< Events skipped by callback dispatch.
  uint64_t fileFailedEvents = 0;                        ///< Events that could not be written to the file sink.
  uint64_t breadcrumbOverwrittenEvents = 0;             ///< Breadcrumb entries overwritten in the ring buffer.
  size_t queueDepth = 0;                                ///< Events currently waiting for the worker.
  size_t queueCapacity = 0;                             ///< Configured queue capacity.
  size_t queueHighWaterMark = 0;                        ///< Maximum observed queue depth.
};

/// Token returned when subscribing a log callback.
using CallbackToken = uint64_t;

/// Starts the logging worker and configured sinks.
/// @param configuration Output paths, queue capacity, and severity thresholds.
/// @return True when logging is initialized; false when initialization cannot start.
ENGINE_API bool Initialize(const Configuration& configuration = {});

/// Stops the worker and releases logging resources.
ENGINE_API void Shutdown() noexcept;

/// Waits for queued events to be processed.
ENGINE_API void Flush() noexcept;

/// Reports whether the logging engine is initialized.
/// @return True while the worker accepts events.
ENGINE_API bool IsInitialized() noexcept;

/// Associates subsequently submitted events with an application frame.
/// @param frameIndex Zero-based frame index.
ENGINE_API void SetFrameIndex(uint64_t frameIndex) noexcept;

/// Returns the operation identifier active on the calling thread.
/// @return Current operation identifier, or zero when no operation is active.
ENGINE_API uint64_t GetCurrentOperationId() noexcept;

/// Sets the operation identifier active on the calling thread.
/// @param operationId Identifier to associate with subsequent events.
ENGINE_API void SetCurrentOperationId(uint64_t operationId) noexcept;

/// Generates a unique operation identifier.
/// @return New nonzero identifier.
ENGINE_API uint64_t CreateOperationId() noexcept;

/// Registers a callback and returns a token used to remove it.
/// @param callback Record consumer.
/// @return Callback token, or zero when registration fails.
ENGINE_API CallbackToken SubscribeCallback(LogCallback callback);

/// Removes a callback registered with SubscribeCallback.
/// @param token Callback token to remove.
ENGINE_API void UnsubscribeCallback(CallbackToken token) noexcept;

/// Replaces the single legacy callback slot.
/// @param callback Record consumer, or an empty function to clear the slot.
ENGINE_API void SetCallback(LogCallback callback);

/// Changes the default severity threshold.
/// @param level Lowest severity to accept.
ENGINE_API void SetMinimumLevel(Level level) noexcept;

/// Returns the default severity threshold.
/// @return Current minimum severity.
ENGINE_API Level GetMinimumLevel() noexcept;

/// Changes a category-specific severity threshold.
/// @param category Category to configure.
/// @param level Lowest severity to accept for that category.
ENGINE_API void SetCategoryMinimumLevel(Category category, Level level) noexcept;

/// Returns a category-specific severity threshold.
/// @param category Category to query.
/// @return Current minimum severity for the category.
ENGINE_API Level GetCategoryMinimumLevel(Category category) noexcept;

/// Returns current queue, sink, and callback health counters.
/// @return Snapshot of logging health state.
ENGINE_API HealthSnapshot GetHealthSnapshot();

/// Returns recently dispatched records in chronological order.
/// @return Copy of the recent-record buffer.
ENGINE_API std::vector<LogRecord> GetRecentRecords();

/// Writes recent crash breadcrumbs to a file.
/// @param path Destination breadcrumb file.
ENGINE_API void DumpCrashBreadcrumbs(const std::filesystem::path& path) noexcept;

/// Enqueues a prebuilt event for asynchronous dispatch.
/// @param event Event to submit.
/// @return True when the event was accepted by the queue.
ENGINE_API bool Enqueue(const LogEvent& event) noexcept;

/// Enqueues an event containing structured fields and a stable error code.
/// @param event Structured event to submit.
/// @return True when the event was accepted by the queue.
ENGINE_API bool SubmitStructuredEvent(const LogEvent& event) noexcept;

/// Associates log events on the current thread with one operation identifier.
class ScopedOperation {
public:
  /// Scoped operations cannot be copied because they restore thread-local state.
  ScopedOperation(const ScopedOperation&) = delete;
  ScopedOperation& operator=(const ScopedOperation&) = delete;
#if defined(ENGINE_ENABLE_LOGGING) && !ENGINE_ENABLE_LOGGING
  /// Creates a no-op scope when logging is disabled.
  ScopedOperation() noexcept = default;

  /// Creates a no-op scope for the supplied operation identifier.
  /// @param operationId Ignored identifier in logging-disabled builds.
  explicit ScopedOperation(uint64_t) noexcept {}
  ~ScopedOperation() = default;
#else
  /// Reuses the active operation identifier or creates one for this scope.
  ScopedOperation() noexcept : m_Previous(GetCurrentOperationId()) {
    SetCurrentOperationId(m_Previous == 0 ? CreateOperationId() : m_Previous);
  }
  /// Sets a specific operation identifier for this scope.
  /// @param operationId Identifier to associate with events in the scope.
  explicit ScopedOperation(uint64_t operationId) noexcept : m_Previous(GetCurrentOperationId()) {
    SetCurrentOperationId(operationId);
  }
  /// Restores the operation identifier that was active before this scope.
  ~ScopedOperation() { SetCurrentOperationId(m_Previous); }

private:
  uint64_t m_Previous;
#endif
};

namespace Detail {
template <typename T> inline LogArgument MakeArgument(T&& input) noexcept {
  using ValueType = std::remove_cvref_t<T>;
  LogArgument result;
  if constexpr (std::is_same_v<ValueType, bool>) {
    result.type = ArgumentType::Boolean;
    result.value.booleanValue = input;
  } else if constexpr (std::is_enum_v<ValueType>) {
    using Underlying = std::underlying_type_t<ValueType>;
    return MakeArgument(static_cast<Underlying>(input));
  } else if constexpr (std::is_integral_v<ValueType> && std::is_signed_v<ValueType>) {
    result.type = ArgumentType::Signed;
    result.value.signedValue = static_cast<int64_t>(input);
  } else if constexpr (std::is_integral_v<ValueType>) {
    result.type = ArgumentType::Unsigned;
    result.value.unsignedValue = static_cast<uint64_t>(input);
  } else if constexpr (std::is_floating_point_v<ValueType>) {
    result.type = ArgumentType::Floating;
    result.value.floatingValue = static_cast<double>(input);
  } else if constexpr (std::convertible_to<T, std::string_view>) {
    result.type = ArgumentType::Text;
    const std::string_view view(input);
    const size_t count = (std::min)(view.size(), MaxLogArgumentText - 1);
    if (count > 0)
      std::copy_n(view.data(), count, result.value.text);
    result.value.text[count] = '\0';
  } else {
    static_assert(sizeof(T) == 0, "Log arguments must be primitive, enum, or string-like values");
  }
  return result;
}

inline void CopyText(char* destination, size_t capacity, std::string_view source) noexcept {
  if (capacity == 0)
    return;
  const size_t count = (std::min)(source.size(), capacity - 1);
  if (count > 0)
    std::copy_n(source.data(), count, destination);
  destination[count] = '\0';
}
} // namespace Detail

template <typename T> inline PayloadEntry MakePayloadEntry(std::string_view key, T&& value) noexcept {
  PayloadEntry entry;
  Detail::CopyText(entry.key, sizeof(entry.key), key);
  entry.value = Detail::MakeArgument(std::forward<T>(value));
  return entry;
}

template <typename... Args>
inline void Submit(Level level, Category category, const char* format, std::source_location location,
                   Args&&... args) noexcept {
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
  std::copy(arguments.begin(), arguments.end(), event.arguments.begin());
  (void)Enqueue(event);
}

template <typename... Args>
inline bool SubmitStructured(Level level, Category category, const char* format, std::string_view errorCode,
                             const std::source_location& location, const PayloadEntry* payload, size_t payloadCount,
                             Args&&... args) noexcept {
  static_assert(sizeof...(Args) <= MaxLogArguments, "At most eight log arguments are supported");
  LogEvent event;
  event.level = level;
  event.category = category;
  event.format = format;
  event.file = location.file_name();
  event.function = location.function_name();
  event.line = location.line();
  Detail::CopyText(event.errorCode, sizeof(event.errorCode), errorCode);
  if (payload != nullptr) {
    const size_t safePayloadCount = (std::min)(payloadCount, MaxLogPayloadEntries);
    std::copy_n(payload, safePayloadCount, event.payload.begin());
    event.payloadCount = static_cast<uint8_t>(safePayloadCount);
  }
  event.argumentCount = static_cast<uint8_t>(sizeof...(Args));
  const std::array<LogArgument, sizeof...(Args)> arguments{Detail::MakeArgument(std::forward<Args>(args))...};
  std::copy(arguments.begin(), arguments.end(), event.arguments.begin());
  return SubmitStructuredEvent(event);
}
} // namespace Engine::Log
