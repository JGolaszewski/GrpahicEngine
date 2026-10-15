#include "runtime/include/LogEngine.h"
#include "runtime/include/EnvironmentVariable.h"

#include <nlohmann/json.hpp>

#include <array>
#include <bit>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#  define NOMINMAX
#  include <Windows.h>
#endif

namespace Engine::Log {
namespace {
using Json = nlohmann::json;
constexpr size_t BreadcrumbCount = 1000;

void FallbackDiagnostic(std::string_view message) noexcept {
  try {
    std::string terminated(message);
    terminated.push_back('\n');
#ifdef _WIN32
    OutputDebugStringA(terminated.c_str());
#endif
    std::fwrite(terminated.data(), 1, terminated.size(), stderr);
    std::fflush(stderr);
  } catch (...) {
    std::fputs("Logger diagnostic failure\n", stderr);
    std::fflush(stderr);
  }
}

auto ParseLevel(const char* value) noexcept -> std::optional<Level> {
  if (value == nullptr) {
    return std::nullopt;
  }
  const std::string_view text(value);
  if (text == "trace" || text == "Trace" || text == "0") {
    return Level::Trace;
  }
  if (text == "debug" || text == "Debug" || text == "1") {
    return Level::Debug;
  }
  if (text == "info" || text == "Info" || text == "2") {
    return Level::Info;
  }
  if (text == "warn" || text == "warning" || text == "Warn" || text == "Warning" || text == "3") {
    return Level::Warning;
  }
  if (text == "error" || text == "Error" || text == "4") {
    return Level::Error;
  }
  if (text == "fatal" || text == "Fatal" || text == "5") {
    return Level::Fatal;
  }
  if (text == "none" || text == "None" || text == "6") {
    return Level::None;
  }
  return std::nullopt;
}

auto DefaultLogPath() -> std::filesystem::path {
#ifdef _WIN32
  if (const auto root = ReadEnvironmentVariable("LOCALAPPDATA")) {
    return std::filesystem::path(*root) / "GraphicEngine" / "logs" / "engine.jsonl";
  }
#else
  if (const auto root = ReadEnvironmentVariable("XDG_STATE_HOME"))
    return std::filesystem::path(*root) / "graphic-engine" / "engine.jsonl";
  if (const auto root = ReadEnvironmentVariable("HOME"))
    return std::filesystem::path(*root) / ".local" / "state" / "graphic-engine" / "engine.jsonl";
#endif
  return std::filesystem::current_path() / "engine.jsonl";
}

auto CreateSessionId() -> std::string {
  const auto now =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  return std::to_string(now) + "-" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

auto ResolveConfiguration(Configuration configuration) -> Configuration {
  if (const auto path = ReadEnvironmentVariable("ENGINE_LOG_PATH")) {
    configuration.jsonlPath = *path;
  } else if (configuration.jsonlPath.empty()) {
    configuration.jsonlPath = DefaultLogPath();
  }

  if (configuration.breadcrumbPath.empty() && !configuration.jsonlPath.empty()) {
    configuration.breadcrumbPath = configuration.jsonlPath.parent_path() / "engine-crash-breadcrumbs.log";
  }
  if (const auto path = ReadEnvironmentVariable("ENGINE_LOG_BREADCRUMB_PATH")) {
    configuration.breadcrumbPath = *path;
  }
  if (const auto limit = ReadEnvironmentVariable("ENGINE_LOG_MAX_FILE_SIZE_BYTES")) {
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(limit->c_str(), &end, 10);
    if (end != limit->c_str() && *end == '\0') {
      configuration.maxFileSizeBytes = static_cast<uint64_t>(parsed);
    }
  }
  if (const auto count = ReadEnvironmentVariable("ENGINE_LOG_RETAINED_FILE_COUNT")) {
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(count->c_str(), &end, 10);
    if (end != count->c_str() && *end == '\0') {
      configuration.retainedFileCount = static_cast<size_t>((std::min)(parsed, 32ULL));
    }
  }
  if (const auto value = ReadEnvironmentVariable("ENGINE_LOG_LEVEL"); value) {
    if (const auto level = ParseLevel(value->c_str())) {
      configuration.minimumLevel = *level;
    }
  }
  constexpr std::array<std::pair<Category, const char*>, 12> categoryVariables{
      {
          {Category::Core, "ENGINE_LOG_CORE_LEVEL"},
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
          {Category::Count, nullptr},
      },
  };
  for (const auto& [category, variable] : categoryVariables) {
    if (variable == nullptr) {
      continue;
    }
    if (const auto value = ReadEnvironmentVariable(variable); value) {
      if (const auto level = ParseLevel(value->c_str())) {
        configuration.categoryMinimumLevels.at(static_cast<size_t>(category)) = *level;
      }
    }
  }
  return configuration;
}

auto ArgumentText(const LogArgument& argument, std::string_view specification) -> std::string {
  const std::string pattern = "{" + (specification.empty() ? std::string{} : ":" + std::string(specification)) + "}";
  const auto format = [&pattern](const auto& value) -> auto {
    try {
      return std::vformat(pattern, std::make_format_args(value));
    } catch (const std::format_error&) {
      return std::string("<format-error>");
    }
  };
  switch (argument.type) {
  case ArgumentType::Signed:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    return format(argument.value.signedValue);
  case ArgumentType::Unsigned:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    return format(argument.value.unsignedValue);
  case ArgumentType::Floating:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    return format(argument.value.floatingValue);
  case ArgumentType::Boolean:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    return format(argument.value.booleanValue);
  case ArgumentType::Text:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    return format(argument.value.text);
  }
  return {};
}

std::string FormatMessage(const LogEvent& event) {
  std::string result;
  const std::string_view format = (event.format != nullptr) ? event.format : "";
  result.reserve(format.size() + (static_cast<size_t>(event.argumentCount) * 16));
  size_t argumentIndex = 0;
  for (size_t i = 0; i < format.size(); ++i) {
    if (format.at(i) == '{' && i + 1 < format.size() && format.at(i + 1) == '{') {
      result.push_back('{');
      ++i;
    } else if (format.at(i) == '}' && i + 1 < format.size() && format.at(i + 1) == '}') {
      result.push_back('}');
      ++i;
    } else if (format.at(i) == '{' && argumentIndex < MaxLogArguments && argumentIndex < event.argumentCount) {
      const size_t close = format.find('}', i + 1);
      if (close != std::string_view::npos) {
        std::string_view specification;
        const size_t colon = format.find(':', i + 1);
        if (colon != std::string_view::npos && colon < close) {
          specification = format.substr(colon + 1, close - colon - 1);
        }
        result += ArgumentText(event.arguments.at(argumentIndex++), specification);
        i = close;
      } else {
        {
          result.push_back(format.at(i));
        }
      }
    } else {
      {
        result.push_back(format.at(i));
      }
    }
  }
  return result;
}

void FallbackEvent(const LogEvent& event) noexcept {
  if (event.level < Level::Error) {
    return;
  }
  try {
    std::string diagnostic = "Critical log event could not be queued [";
    diagnostic += ToString(event.level);
    diagnostic += "][";
    diagnostic += ToString(event.category);
    diagnostic += ']';
    if (!ErrorCodeView(event).empty()) {
      diagnostic += " [";
      diagnostic += ErrorCodeView(event);
      diagnostic += ']';
    }
    diagnostic += ": ";
    diagnostic += FormatMessage(event);
    FallbackDiagnostic(diagnostic);
  } catch (...) {
    FallbackDiagnostic("Critical log event could not be queued");
  }
}

auto ArgumentJson(const LogArgument& argument) -> Json {
  switch (argument.type) {
  case ArgumentType::Signed:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    return argument.value.signedValue;
  case ArgumentType::Unsigned:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    return argument.value.unsignedValue;
  case ArgumentType::Floating:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    return argument.value.floatingValue;
  case ArgumentType::Boolean:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    return argument.value.booleanValue;
  case ArgumentType::Text:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    return argument.value.text;
  }
  return nullptr;
}

auto PayloadJson(const LogEvent& event) -> Json {
  Json payload = Json::object();
  for (size_t index = 0; index < event.payloadCount; ++index) {
    const PayloadEntry& field = event.payload.at(index);
    payload.at(field.key) = ArgumentJson(field.value);
  }
  return payload;
}

auto ArgumentsJson(const LogEvent& event) -> Json {
  Json arguments = Json::array();
  for (size_t index = 0; index < event.argumentCount; ++index) {
    arguments.push_back(ArgumentJson(event.arguments.at(index)));
  }
  return arguments;
}

struct QueueCell {
  std::atomic<size_t> sequence{0};
  LogEvent event{};
};

struct Manager {
  explicit Manager(Configuration config)
      : configuration(std::move(config)), capacity((std::max)(size_t{2}, configuration.queueCapacity)),
        mask(capacity - 1), minimumLevel(static_cast<uint8_t>(configuration.minimumLevel)) {
    // Bounded MPMC sequence queues require a power-of-two capacity.
    capacity = std::bit_ceil(capacity);

    // Atomic queue cells are non-movable and require runtime-sized contiguous storage.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
    queue = std::make_unique<QueueCell[]>(capacity);
    for (size_t i = 0; i < capacity; ++i) {
      std::span<QueueCell>(queue.get(), capacity).subspan(i, 1).front().sequence.store(i, std::memory_order_relaxed);
    }
    for (size_t i = 0; i < CategoryCount; ++i) {
      categoryMinimumLevels.at(i).store(static_cast<uint8_t>(configuration.categoryMinimumLevels.at(i)),
                                        std::memory_order_relaxed);
    }
  }

  Configuration configuration;
  std::string sessionId = CreateSessionId();
  size_t capacity;
  size_t mask;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
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

  auto Accepts(const LogEvent& event) const noexcept -> bool {
    const auto level = static_cast<uint8_t>(event.level);
    const size_t category = static_cast<size_t>(event.category);
    const uint8_t globalMinimum = minimumLevel.load(std::memory_order_relaxed);
    const uint8_t categoryMinimum =
        category < CategoryCount ? categoryMinimumLevels.at(category).load(std::memory_order_relaxed) : globalMinimum;
    return level >= (std::max)(globalMinimum, categoryMinimum);
  }

  void SetSinkFailure(const std::string& message) {
    fileStatus.store(FileSinkStatus::Failed, std::memory_order_release);
    fileFailedEvents.fetch_add(1, std::memory_order_relaxed);
    {
      std::scoped_lock lock(sinkErrorMutex);
      lastSinkError = message;
    }
    FallbackDiagnostic(message);
  }

  auto RotateFile() -> bool {
    if ((configuration.maxFileSizeBytes == 0U) || (jsonlBytes == 0U) || jsonlBytes < configuration.maxFileSizeBytes) {
      return true;
    }
    jsonl.flush();
    jsonl.close();
    std::error_code error;
    if (configuration.retainedFileCount == 0) {
      std::filesystem::remove(configuration.jsonlPath, error);
    } else {
      const std::filesystem::path oldest =
          configuration.jsonlPath.string() + "." + std::to_string(configuration.retainedFileCount);
      std::filesystem::remove(oldest, error);
      error.clear();
      for (size_t index = configuration.retainedFileCount; index > 1; --index) {
        const auto from = std::filesystem::path(configuration.jsonlPath.string() + "." + std::to_string(index - 1));
        const auto to = std::filesystem::path(configuration.jsonlPath.string() + "." + std::to_string(index));
        if (std::filesystem::exists(from, error)) {
          std::filesystem::rename(from, to, error);
          if (error) {
            break;
          }
        }
        error.clear();
      }
      if (!error) {
        std::filesystem::rename(configuration.jsonlPath, configuration.jsonlPath.string() + ".1", error);
      }
    }
    if (error) {
      SetSinkFailure("Log file rotation failed: " + error.message());
      return false;
    }
    jsonl.open(configuration.jsonlPath, std::ios::out | std::ios::trunc);
    if (!jsonl) {
      SetSinkFailure("Log file reopen after rotation failed: " + configuration.jsonlPath.string());
      return false;
    }
    jsonlBytes = 0;
    fileStatus.store(FileSinkStatus::Ready, std::memory_order_release);
    return true;
  }

  auto Push(const LogEvent& event) noexcept -> bool {
    size_t position = enqueuePosition.load(std::memory_order_relaxed);
    QueueCell* cell = nullptr;
    for (;;) {
      cell = &std::span<QueueCell>(queue.get(), capacity).subspan(position & mask, 1).front();
      const size_t sequence = cell->sequence.load(std::memory_order_acquire);
      const intptr_t difference = static_cast<intptr_t>(sequence) - static_cast<intptr_t>(position);
      if (difference == 0) {
        if (enqueuePosition.compare_exchange_weak(position, position + 1, std::memory_order_relaxed)) {
          break;
        }
      } else if (difference < 0) {
        {
          return false;
        }
      } else {
        {
          position = enqueuePosition.load(std::memory_order_relaxed);
        }
      }
    }
    cell->event = event;
    cell->sequence.store(position + 1, std::memory_order_release);
    return true;
  }

  auto Pop(LogEvent& event) noexcept -> bool {
    size_t position = dequeuePosition.load(std::memory_order_relaxed);
    QueueCell* cell = nullptr;
    for (;;) {
      cell = &std::span<QueueCell>(queue.get(), capacity).subspan(position & mask, 1).front();
      const size_t sequence = cell->sequence.load(std::memory_order_acquire);
      const intptr_t difference = static_cast<intptr_t>(sequence) - static_cast<intptr_t>(position + 1);
      if (difference == 0) {
        if (dequeuePosition.compare_exchange_weak(position, position + 1, std::memory_order_relaxed)) {
          break;
        }
      } else if (difference < 0) {
        {
          return false;
        }
      } else {
        {
          position = dequeuePosition.load(std::memory_order_relaxed);
        }
      }
    }
    event = cell->event;
    cell->sequence.store(position + capacity, std::memory_order_release);
    return true;
  }

  void Process(const LogEvent& event) {
    LogRecord record{.event = event, .text = FormatMessage(event)};
    Json payload = PayloadJson(event);
    Json arguments = ArgumentsJson(event);

    Json object = {
        {"timestamp_ns", event.timestampNs},
        {"monotonic_ns", event.monotonicNs},
        {"frame_index", event.frameIndex},
        {"operation_id", event.operationId},
        {"session_id", sessionId},
        {"level", ToString(event.level)},
        {"category", ToString(event.category)},
        {"file", (event.file != nullptr) ? event.file : ""},
        {"line", event.line},
        {"function", (event.function != nullptr) ? event.function : ""},
        {"error_code", !ErrorCodeView(event).empty() ? Json(ErrorCodeView(event)) : Json(nullptr)},
        {"message", record.text},
        {"payload", std::move(payload)},
        {"arguments", std::move(arguments)},
        {"thread_id", event.threadId},
    };
    {
      std::scoped_lock lock(ioMutex);
      if (jsonl.is_open() && RotateFile() && jsonl.is_open()) {
        const std::string line = object.dump() + '\n';
        jsonl.write(line.data(), static_cast<std::streamsize>(line.size()));
        if (!jsonl) {
          jsonl.close();
          SetSinkFailure("Log file write failed: " + configuration.jsonlPath.string());
        } else {
          jsonlBytes += line.size();
          writtenEvents.fetch_add(1, std::memory_order_relaxed);
        }
        if (event.level >= Level::Error) {
          jsonl.flush();
          if (!jsonl) {
            jsonl.close();
            SetSinkFailure("Log file flush failed: " + configuration.jsonlPath.string());
          }
        }
      }
    }

    std::vector<CallbackToken> activeCallbacks;
    {
      std::scoped_lock lock(sinkMutex);
      if (breadcrumbSize == BreadcrumbCount) {
        breadcrumbOverwrittenEvents.fetch_add(1, std::memory_order_relaxed);
      }
      breadcrumbs.at(breadcrumbWrite) = record;
      breadcrumbWrite = (breadcrumbWrite + 1) % BreadcrumbCount;
      breadcrumbSize = (std::min)(breadcrumbSize + 1, BreadcrumbCount);
      activeCallbacks.reserve(callbacks.size());
      for (const auto& [token, callback] : callbacks) {
        (void)token;
        activeCallbacks.push_back(token);
      }
    }
    if (!activeCallbacks.empty()) {
      bool queueFull = false;
      try {
        std::scoped_lock lock(callbackQueueMutex);
        if (callbackQueue.size() < capacity) {
          callbackQueue.emplace_back(record, std::move(activeCallbacks));
          callbackWake.notify_one();
        } else {
          {
            queueFull = true;
          }
        }
      } catch (...) {
        queueFull = true;
      }
      if (queueFull) {
        callbackDroppedEvents.fetch_add(1, std::memory_order_relaxed);
      }
    }
    if (event.level == Level::Fatal && !configuration.breadcrumbPath.empty()) {
      WriteBreadcrumbs(configuration.breadcrumbPath);
    }
  }

  void WriteBreadcrumbs(const std::filesystem::path& path) noexcept {
    try {
      std::scoped_lock lock(sinkMutex);
      std::error_code error;
      if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
      }
      if (error) {
        FallbackDiagnostic("Unable to create breadcrumb directory: " + error.message());
        return;
      }
      std::ofstream output(path, std::ios::out | std::ios::trunc);
      if (!output) {
        FallbackDiagnostic("Unable to persist log breadcrumbs: " + path.string());
        return;
      }
      const size_t start = (breadcrumbWrite + BreadcrumbCount - breadcrumbSize) % BreadcrumbCount;
      for (size_t i = 0; i < breadcrumbSize; ++i) {
        const LogRecord& record = breadcrumbs.at((start + i) % BreadcrumbCount);
        output << record.event.timestampNs << " [" << ToString(record.event.level) << "]["
               << ToString(record.event.category) << "][frame=" << record.event.frameIndex
               << "][operation=" << record.event.operationId << "]";
        if (!ErrorCodeView(record.event).empty()) {
          output << "[code=" << ErrorCodeView(record.event) << "]";
        }
        output << ' ' << ((record.event.file != nullptr) ? record.event.file : "") << ':' << record.event.line << ' '
               << record.text << '\n';
      }
      output.flush();
      if (!output) {
        FallbackDiagnostic("Unable to complete log breadcrumb write: " + path.string());
      }
    } catch (...) {
      FallbackDiagnostic("Unexpected error while persisting log breadcrumbs");
    }
  }

  void RunCallbacks() {
    for (;;) {
      std::pair<LogRecord, std::vector<CallbackToken>> work;
      {
        std::unique_lock lock(callbackQueueMutex);
        callbackWake.wait(lock, [this] -> bool { return callbackStopping || !callbackQueue.empty(); });
        if (callbackQueue.empty() && callbackStopping) {
          break;
        }
        work = std::move(callbackQueue.front());
        callbackQueue.pop_front();
      }
      for (CallbackToken token : work.second) {
        LogCallback callback;
        {
          std::scoped_lock lock(sinkMutex);
          const auto found = callbacks.find(token);
          if (found != callbacks.end()) {
            callback = found->second;
          }
        }
        if (!callback) {
          continue;
        }
        try {
          callback(work.first);
        } catch (...) {
          callbackFailedEvents.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  }

  void WriteSessionHeader() {
    const std::string header =
        Json{
            {"type", "session_start"},
            {"session_id", sessionId},
            {
                "timestamp_ns",
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count(),
            },
        }
            .dump() +
        '\n';
    jsonl.write(header.data(), static_cast<std::streamsize>(header.size()));
    if (!jsonl) {
      jsonl.close();
      SetSinkFailure("Unable to write the log session header: " + configuration.jsonlPath.string());
      return;
    }
    jsonlBytes += header.size();
    fileStatus.store(FileSinkStatus::Ready, std::memory_order_release);
  }

  void OpenFileSink() {
    try {
      if (configuration.jsonlPath.empty()) {
        fileStatus.store(FileSinkStatus::Disabled, std::memory_order_release);
        return;
      }
      fileStatus.store(FileSinkStatus::Opening, std::memory_order_release);
      std::error_code error;
      if (configuration.jsonlPath.has_parent_path()) {
        std::filesystem::create_directories(configuration.jsonlPath.parent_path(), error);
      }
      if (error) {
        SetSinkFailure("Unable to create log directory: " + error.message());
        return;
      }
      jsonl.open(configuration.jsonlPath, std::ios::out | std::ios::app);
      if (!jsonl) {
        SetSinkFailure("Unable to open log file: " + configuration.jsonlPath.string());
        return;
      }
      jsonlBytes = std::filesystem::file_size(configuration.jsonlPath, error);
      if (error) {
        jsonlBytes = 0;
      }
      WriteSessionHeader();
    } catch (...) {
      if (jsonl.is_open()) {
        jsonl.close();
      }
      SetSinkFailure("Unexpected error while opening the log file");
    }
  }

  void Run() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
    try {
      OpenFileSink();
    } catch (...) {
      fileStatus.store(FileSinkStatus::Failed, std::memory_order_release);
      FallbackDiagnostic("Unexpected exception while opening the log file");
    }
    {
      std::scoped_lock lock(startupMutex);
      startupComplete = true;
    }
    startupReady.notify_all();
    for (;;) {
      LogEvent event;
      if (Pop(event)) {
        try {
          Process(event);
        } catch (...) {
          FallbackDiagnostic("Log event processing failed");
        }
        pending.fetch_sub(1, std::memory_order_release);
        drained.notify_all();
        continue;
      }
      if (stopping.load(std::memory_order_acquire) && pending.load(std::memory_order_acquire) == 0) {
        break;
      }
      std::unique_lock lock(wakeMutex);
      wake.wait_for(lock, std::chrono::milliseconds(10), [this] -> bool {
        return stopping.load(std::memory_order_relaxed) || pending.load(std::memory_order_relaxed) != 0;
      });
    }
    {
      std::scoped_lock lock(ioMutex);
      if (jsonl.is_open()) {
        jsonl.flush();
        if (!jsonl) {
          jsonl.close();
          SetSinkFailure("Log file flush failed during shutdown: " + configuration.jsonlPath.string());
        }
      }
    }
  }
};

struct LoggerGlobals {
  std::atomic<Manager*> manager{nullptr};
  std::unique_ptr<Manager> managerOwner;
  std::mutex lifecycleMutex;
  bool shuttingDown = false;
  std::atomic<uint64_t> frameIndex{0};
  std::atomic<uint64_t> nextOperationId{1};
  std::atomic<size_t> activeProducers{0};
  std::atomic<uint8_t> minimumLevel{static_cast<uint8_t>(Level::Trace)};
  std::array<std::atomic<uint8_t>, CategoryCount> categoryMinimumLevels{};
  std::atomic<CallbackToken> nextCallbackToken{1};
  std::mutex legacyCallbackMutex;
  CallbackToken legacyCallbackToken = 0;
};

auto GetLoggerGlobals() -> LoggerGlobals& {
  static LoggerGlobals globals;
  return globals;
}

auto CurrentOperationId() -> uint64_t& {
  thread_local uint64_t operationId = 0;
  return operationId;
}
} // namespace

auto ToString(Level level) noexcept -> const char* {
  switch (level) {
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

auto ToString(Category category) noexcept -> const char* {
  switch (category) {
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

auto CategoryFromName(std::string_view name) noexcept -> Category {
  if (name == "Render") {
    return Category::Render;
  }
  if (name == "Editor") {
    return Category::Editor;
  }
  if (name == "Backend") {
    return Category::Backend;
  }
  if (name == "Diagnostics") {
    return Category::Diagnostics;
  }
  if (name == "Physics") {
    return Category::Physics;
  }
  if (name == "Audio") {
    return Category::Audio;
  }
  if (name == "ECS") {
    return Category::ECS;
  }
  if (name == "Net") {
    return Category::Net;
  }
  if (name == "Network") {
    return Category::Network;
  }
  if (name == "Asset") {
    return Category::Asset;
  }
  return Category::Core;
}

auto Initialize(const Configuration& configuration) -> bool {
  std::scoped_lock lock(GetLoggerGlobals().lifecycleMutex);
  if (GetLoggerGlobals().shuttingDown) {
    return false;
  }
  if (GetLoggerGlobals().manager.load(std::memory_order_acquire) != nullptr) {
    return GetLoggerGlobals().manager.load(std::memory_order_relaxed)->fileStatus.load(std::memory_order_acquire) !=
           FileSinkStatus::Failed;
  }
  std::unique_ptr<Manager> manager;
  try {
    Configuration resolved = ResolveConfiguration(configuration);
    GetLoggerGlobals().minimumLevel.store(static_cast<uint8_t>(resolved.minimumLevel), std::memory_order_relaxed);
    for (size_t i = 0; i < CategoryCount; ++i) {
      GetLoggerGlobals().categoryMinimumLevels.at(i).store(static_cast<uint8_t>(resolved.categoryMinimumLevels.at(i)),
                                                           std::memory_order_relaxed);
    }
    manager = std::make_unique<Manager>(std::move(resolved));
  } catch (...) {
    FallbackDiagnostic("Unable to allocate the logger state");
    return false;
  }
  try {
    manager->worker = std::thread([managerPointer = manager.get()] -> void { managerPointer->Run(); });
  } catch (...) {
    FallbackDiagnostic("Unable to start the logger worker thread");
    return false;
  }
  try {
    manager->callbackWorker = std::thread([managerPointer = manager.get()] -> void { managerPointer->RunCallbacks(); });
  } catch (...) {
    manager->stopping.store(true, std::memory_order_release);
    manager->wake.notify_one();
    if (manager->worker.joinable()) {
      manager->worker.join();
    }
    FallbackDiagnostic("Unable to start the logger callback worker thread");
    return false;
  }
  Manager* managerPointer = manager.get();
  GetLoggerGlobals().managerOwner = std::move(manager);
  GetLoggerGlobals().manager.store(managerPointer, std::memory_order_release);
  {
    std::unique_lock startupLock(managerPointer->startupMutex);
    managerPointer->startupReady.wait(startupLock,
                                      [managerPointer] -> bool { return managerPointer->startupComplete; });
  }
  return managerPointer->fileStatus.load(std::memory_order_acquire) != FileSinkStatus::Failed;
}

void Shutdown() noexcept {
  std::unique_ptr<Manager> manager;
  {
    std::scoped_lock lock(GetLoggerGlobals().lifecycleMutex);
    Manager* managerPointer = GetLoggerGlobals().manager.exchange(nullptr, std::memory_order_acq_rel);
    if (managerPointer == nullptr) {
      return;
    }
    manager = std::move(GetLoggerGlobals().managerOwner);
    GetLoggerGlobals().shuttingDown = true;
  }
  while (GetLoggerGlobals().activeProducers.load(std::memory_order_acquire) != 0) {
    std::this_thread::yield();
  }
  manager->stopping.store(true, std::memory_order_release);
  manager->wake.notify_one();
  if (manager->worker.joinable()) {
    manager->worker.join();
  }
  {
    std::scoped_lock callbackLock(manager->callbackQueueMutex);
    manager->callbackStopping = true;
  }
  manager->callbackWake.notify_one();
  if (manager->callbackWorker.joinable()) {
    manager->callbackWorker.join();
  }
  {
    std::scoped_lock lock(GetLoggerGlobals().lifecycleMutex);
    GetLoggerGlobals().shuttingDown = false;
  }
}

void Flush() noexcept {
  Manager* manager = GetLoggerGlobals().manager.load(std::memory_order_acquire);
  if (manager == nullptr) {
    return;
  }
  try {
    std::unique_lock drainLock(manager->drainMutex);
    if (!manager->drained.wait_for(drainLock, std::chrono::seconds(5), [manager] -> bool {
          return manager->pending.load(std::memory_order_acquire) == 0;
        })) {
      FallbackDiagnostic("Timed out while flushing the log queue");
    }
    drainLock.unlock();
    std::scoped_lock lock(manager->ioMutex);
    if (manager->jsonl.is_open()) {
      manager->jsonl.flush();
      if (!manager->jsonl) {
        manager->jsonl.close();
        manager->SetSinkFailure("Log file flush failed: " + manager->configuration.jsonlPath.string());
      }
    }
  } catch (...) {
    FallbackDiagnostic("Unexpected error while flushing the log queue");
  }
}

auto IsInitialized() noexcept -> bool { return GetLoggerGlobals().manager.load(std::memory_order_acquire) != nullptr; }

void SetFrameIndex(uint64_t frameIndex) noexcept {
  GetLoggerGlobals().frameIndex.store(frameIndex, std::memory_order_relaxed);
}

auto GetCurrentOperationId() noexcept -> uint64_t { return CurrentOperationId(); }

void SetCurrentOperationId(uint64_t operationId) noexcept { CurrentOperationId() = operationId; }

auto CreateOperationId() noexcept -> uint64_t {
  return GetLoggerGlobals().nextOperationId.fetch_add(1, std::memory_order_relaxed);
}

auto SubscribeCallback(LogCallback callback) -> CallbackToken {
  if (!callback) {
    return 0;
  }
  const CallbackToken token = GetLoggerGlobals().nextCallbackToken.fetch_add(1, std::memory_order_relaxed);
  if (Manager* manager = GetLoggerGlobals().manager.load(std::memory_order_acquire)) {
    std::scoped_lock lock(manager->sinkMutex);
    manager->callbacks.emplace(token, std::move(callback));
    return token;
  }
  return 0;
}

void UnsubscribeCallback(CallbackToken token) noexcept {
  if (token == 0) {
    return;
  }
  if (Manager* manager = GetLoggerGlobals().manager.load(std::memory_order_acquire)) {
    std::scoped_lock lock(manager->sinkMutex);
    manager->callbacks.erase(token);
  }
}

void SetCallback(LogCallback callback) {
  std::scoped_lock lock(GetLoggerGlobals().legacyCallbackMutex);
  UnsubscribeCallback(GetLoggerGlobals().legacyCallbackToken);
  GetLoggerGlobals().legacyCallbackToken = callback ? SubscribeCallback(std::move(callback)) : 0;
}

void SetMinimumLevel(Level level) noexcept {
  GetLoggerGlobals().minimumLevel.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
  if (Manager* manager = GetLoggerGlobals().manager.load(std::memory_order_acquire)) {
    manager->minimumLevel.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
  }
}

auto GetMinimumLevel() noexcept -> Level {
  return static_cast<Level>(GetLoggerGlobals().minimumLevel.load(std::memory_order_relaxed));
}

void SetCategoryMinimumLevel(Category category, Level level) noexcept {
  const size_t index = static_cast<size_t>(category);
  if (index >= CategoryCount) {
    return;
  }
  GetLoggerGlobals().categoryMinimumLevels.at(index).store(static_cast<uint8_t>(level), std::memory_order_relaxed);
  if (Manager* manager = GetLoggerGlobals().manager.load(std::memory_order_acquire)) {
    manager->categoryMinimumLevels.at(index).store(static_cast<uint8_t>(level), std::memory_order_relaxed);
  }
}

auto GetCategoryMinimumLevel(Category category) noexcept -> Level {
  const size_t index = static_cast<size_t>(category);
  if (index >= CategoryCount) {
    return GetMinimumLevel();
  }
  return static_cast<Level>(GetLoggerGlobals().categoryMinimumLevels.at(index).load(std::memory_order_relaxed));
}

auto GetHealthSnapshot() -> HealthSnapshot {
  HealthSnapshot result;
  if (Manager* manager = GetLoggerGlobals().manager.load(std::memory_order_acquire)) {
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
    result.breadcrumbOverwrittenEvents = manager->breadcrumbOverwrittenEvents.load(std::memory_order_relaxed);
    result.queueDepth = manager->pending.load(std::memory_order_relaxed);
    result.queueCapacity = manager->capacity;
    result.queueHighWaterMark = manager->queueHighWaterMark.load(std::memory_order_relaxed);
    std::scoped_lock lock(manager->sinkErrorMutex);
    result.lastSinkError = manager->lastSinkError;
  }
  return result;
}

auto GetRecentRecords() -> std::vector<LogRecord> {
  if (Manager* manager = GetLoggerGlobals().manager.load(std::memory_order_acquire)) {
    std::scoped_lock lock(manager->sinkMutex);
    std::vector<LogRecord> records;
    records.reserve(manager->breadcrumbSize);
    const size_t start = (manager->breadcrumbWrite + BreadcrumbCount - manager->breadcrumbSize) % BreadcrumbCount;
    for (size_t i = 0; i < manager->breadcrumbSize; ++i) {
      records.push_back(manager->breadcrumbs.at((start + i) % BreadcrumbCount));
    }
    return records;
  }
  return {};
}

void DumpCrashBreadcrumbs(const std::filesystem::path& path) noexcept {
  Manager* manager = GetLoggerGlobals().manager.load(std::memory_order_acquire);
  if (manager == nullptr) {
    return;
  }
  Flush();
  manager->WriteBreadcrumbs(path);
}

auto Enqueue(const LogEvent& source) noexcept -> bool {
  GetLoggerGlobals().activeProducers.fetch_add(1, std::memory_order_acq_rel);
  Manager* manager = GetLoggerGlobals().manager.load(std::memory_order_acquire);
  if ((manager == nullptr) || manager->stopping.load(std::memory_order_relaxed)) {
    FallbackEvent(source);
    GetLoggerGlobals().activeProducers.fetch_sub(1, std::memory_order_release);
    return false;
  }
  LogEvent event = source;
  if (!manager->Accepts(event) && event.level < Level::Error) {
    GetLoggerGlobals().activeProducers.fetch_sub(1, std::memory_order_release);
    return false;
  }
  event.timestampNs = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
  event.monotonicNs = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());
  event.frameIndex = GetLoggerGlobals().frameIndex.load(std::memory_order_relaxed);
  event.threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
  if (event.operationId == 0) {
    event.operationId = CurrentOperationId();
  }
  const size_t depth = manager->pending.fetch_add(1, std::memory_order_relaxed) + 1;
  size_t highWater = manager->queueHighWaterMark.load(std::memory_order_relaxed);
  while (depth > highWater &&
         !manager->queueHighWaterMark.compare_exchange_weak(highWater, depth, std::memory_order_relaxed)) {
  }
  if (!manager->Push(event)) {
    manager->pending.fetch_sub(1, std::memory_order_release);
    manager->queueDroppedEvents.fetch_add(1, std::memory_order_relaxed);
    FallbackEvent(event);
    manager->drained.notify_all();
    GetLoggerGlobals().activeProducers.fetch_sub(1, std::memory_order_release);
    return false;
  }
  manager->acceptedEvents.fetch_add(1, std::memory_order_relaxed);
  manager->wake.notify_one();
  GetLoggerGlobals().activeProducers.fetch_sub(1, std::memory_order_release);
  return true;
}

auto SubmitStructuredEvent(const LogEvent& event) noexcept -> bool { return Enqueue(event); }
} // namespace Engine::Log
