#pragma once

#include "runtime/include/CoreExport.h"
#include "runtime/include/PrismNdfData.h"
#include "runtime/include/Result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Engine {

/// Observable state for a PRISM generation request. Poll this value on the
/// owning/UI thread; the service never invokes consumer callbacks from workers.
enum class PrismNdfInferencePhase : uint8_t {
  Queued,
  Tokenizing,
  Encoding,
  Retrieving,
  Decoding,
  Fitting,
  Completed,
  Failed,
  Cancelled
};

/// Immutable snapshot copied from the service completion queue.
struct PrismNdfInferenceStatus {
  uint64_t requestId = 0;
  PrismNdfInferencePhase phase = PrismNdfInferencePhase::Queued;
  float progress = 0.0F;
  std::string message;
  std::shared_ptr<const PrismNdfData> result;
  std::string prompt;
  bool directional = false;
  std::string modelProvenance;
};

/// Paths to the bundled model directory and embedded Python runtime.
struct PrismNdfInferenceOptions {
  std::filesystem::path modelDirectory;
  std::filesystem::path pythonExecutable;
  std::filesystem::path tokenizerWorker;
};

/// Asynchronous, single-flight PRISM service. Start, Poll, and Cancel are
/// thread-safe. Poll from the Editor thread to consume immutable snapshots.
/// When inference support is disabled, construction still succeeds and Start
/// returns an actionable status explaining how to enable the bundled runtime.
class PrismNdfInferenceService final {
public:
  ENGINE_API explicit PrismNdfInferenceService(PrismNdfInferenceOptions options);
  ENGINE_API ~PrismNdfInferenceService();
  PrismNdfInferenceService(const PrismNdfInferenceService&) = delete;
  auto operator=(const PrismNdfInferenceService&) -> PrismNdfInferenceService& = delete;
  PrismNdfInferenceService(PrismNdfInferenceService&&) = delete;
  auto operator=(PrismNdfInferenceService&&) -> PrismNdfInferenceService& = delete;

  /// Queue one prompt and directional-mode request. Only one request may be
  /// active; the previous terminal snapshot is discarded when the next request
  /// starts so completed payload memory does not accumulate.
  [[nodiscard]] ENGINE_API auto Start(std::string prompt, bool directional) -> Result<uint64_t>;
  /// Returns a snapshot or no value while the request has not been recorded.
  [[nodiscard]] ENGINE_API auto Poll(uint64_t requestId) const -> std::optional<PrismNdfInferenceStatus>;
  /// Requests cancellation; completion is reported by a later Poll.
  [[nodiscard]] ENGINE_API auto Cancel(uint64_t requestId) -> Status;
  /// Whether this build includes native ONNX Runtime support.
  [[nodiscard]] ENGINE_API static auto IsAvailable() noexcept -> bool;

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Engine
