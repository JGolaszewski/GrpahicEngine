#pragma once

#include "rendering/include/RenderOutput.h"
#include "rendering/include/renderer/CameraView.h"
#include "rendering/include/renderer/Material.h"
#include "runtime/include/CoreExport.h"
#include "runtime/include/Result.h"
#include "scene/include/SceneDocument.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace Engine::Renderer::BrdfAnalysis {

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // Same-toolchain engine DLL APIs intentionally expose standard-library state.
#endif

/// Serializable frame-local changes shared by every model variant.
struct ENGINE_API BrdfAnalysisOverrides {
  struct Resolution {
    uint32_t Width = 0;  ///< Frame-local render width; zero is invalid when supplied.
    uint32_t Height = 0; ///< Frame-local render height; zero is invalid when supplied.
  };
  std::optional<Resolution> OutputResolution; ///< Frame-local output size; absence uses snapshot dimensions.
  std::optional<float> Roughness;             ///< Frame-local perceptual roughness applied to every model.
  std::optional<float> GtrGamma;              ///< Frame-local GTR distribution parameter applied to every model.
};

/// Immutable-by-convention inputs captured for repeatable BRDF comparison.
/// The owned scene document and material values are copies; analysis variants
/// are derived from them without changing authored scene or asset state. Explicit
/// frame overrides are stored separately and applied equally to every variant.
struct ENGINE_API BrdfAnalysisSnapshot {
  Engine::SceneDocument Scene;                  ///< Complete authored scene state at capture time.
  uint64_t TargetEntityId = 0;                  ///< Document-local id of the entity containing the selected material.
  std::optional<size_t> MaterialOverrideIndex;  ///< Per-submesh override slot, or empty for the entity material.
  Engine::SceneMaterialDocument SourceMaterial; ///< Exact serialized material data, including texture identities.
  MaterialProperties SourceMaterialProperties;  ///< Resolved numeric factors used by CPU BRDF measurements.
  CameraView Camera;                            ///< Resolved world-space camera pose and projection.
  uint32_t Width = 0;                           ///< Render width captured for every comparison variant.
  uint32_t Height = 0;                          ///< Render height captured for every comparison variant.
  uint32_t ShadowMapResolution = 0;             ///< Active renderer shadow-map resolution at capture time.
  uint32_t MaxShadowCastersPerScene = 0;        ///< Active renderer shadow-caster limit at capture time.
  Engine::FinalColorPostProcess PostProcess;    ///< Final-color processing captured for every variant.
  BrdfAnalysisOverrides FrameOverrides;         ///< Frame-local settings, separate from authored scene/material data.
};

/// Complete immutable request for rendering one BRDF model from a snapshot.
/// It carries the same copied scene and captured render settings as every other
/// variant, with only the model plus explicit frame-local overrides applied.
struct ENGINE_API BrdfAnalysisRenderRequest {
  Engine::SceneDocument Scene;               ///< Per-variant copy of the captured scene.
  BrdfModel Model = BrdfModel::Legacy;       ///< BRDF model selected for this variant.
  MaterialProperties Material;               ///< Captured material factors with only Model changed.
  CameraView Camera;                         ///< Captured camera settings shared by all variants.
  uint32_t Width = 0;                        ///< Captured render width.
  uint32_t Height = 0;                       ///< Captured render height.
  uint32_t ShadowMapResolution = 0;          ///< Captured renderer shadow-map resolution.
  uint32_t MaxShadowCastersPerScene = 0;     ///< Captured renderer shadow-caster limit.
  Engine::FinalColorPostProcess PostProcess; ///< Captured final-color processing.
  BrdfAnalysisOverrides FrameOverrides;      ///< Serializable frame-local settings applied to this variant.
};

/// Render request and output for one completed comparison variant.
struct ENGINE_API BrdfAnalysisVariant {
  BrdfAnalysisRenderRequest Request; ///< Inputs used to produce this output.
  Engine::RenderOutput Output;       ///< CPU-owned render result for scene luminance measurement and display.
};

/// Renders the supplied immutable request and returns its CPU-owned output.
using BrdfAnalysisRenderCallback =
    std::function<Engine::Result<Engine::RenderOutput>(const BrdfAnalysisRenderRequest& request)>;

/// Reports completion of one sequential variant render.
using BrdfAnalysisProgressCallback = std::function<void(size_t completed, size_t total, BrdfModel model)>;

/// Capture controls for sequential, cancellable BRDF variant execution.
struct ENGINE_API BrdfAnalysisCaptureOptions {
  BrdfAnalysisProgressCallback Progress; ///< Called after each completed render; may be empty.
  std::function<bool()> IsCancelled;     ///< Queried before and after each render; may be empty.
};

/// Outcome distinguishes user cancellation from a completed capture.
struct ENGINE_API BrdfAnalysisCaptureResult {
  enum class State : uint8_t { Completed, Cancelled };
  State Status = State::Completed;           ///< Completed capture or cancellation with no returned partial data.
  std::vector<BrdfAnalysisVariant> Variants; ///< Full ordered result; empty when cancelled.
};

/// Lifecycle of a resumable capture session.
enum class BrdfAnalysisCaptureSessionState : uint8_t { Ready, Completed, Cancelled, Failed };

/// Main-thread capture plan that renders one BRDF model per caller-selected step.
/// Partial variants remain private and are discarded on cancellation or failure.
class ENGINE_API BrdfAnalysisCaptureSession {
public:
  /// Validates and owns a snapshot and unique list of implemented models.
  /// @param snapshot Scene, material, camera, renderer settings, and frame overrides.
  /// @param models Unique BRDF models to render in order.
  /// @return A ready capture session, or an error for invalid capture inputs.
  static auto Create(const BrdfAnalysisSnapshot& snapshot, std::span<const BrdfModel> models)
      -> Engine::Result<BrdfAnalysisCaptureSession>;

  /// Returns the current render request, creating it once; null means no renders remain.
  /// The pointer remains valid until completion, cancellation, or session destruction.
  /// @return Current request, or null when complete/cancelled.
  auto GetNextRequest() -> Engine::Result<const BrdfAnalysisRenderRequest*>;
  /// Validates and stores the output for the current request, then advances one model.
  /// Any validation failure clears all accumulated variants and marks the session failed.
  /// @param output CPU-owned output for the current request.
  /// @return Success after advancing, or an error that discards the session's partial data.
  auto CompleteCurrent(Engine::RenderOutput output) -> Engine::Status;
  /// Cancels the session and discards all partial variants.
  void Cancel() noexcept;
  /// Returns current lifecycle status.
  auto GetStatus() const noexcept -> BrdfAnalysisCaptureSessionState { return m_Status; }
  /// Returns the number of fully completed renders, or zero after cancel/failure.
  auto GetCompletedCount() const noexcept -> size_t { return m_Variants.size(); }
  /// Returns the requested model count.
  auto GetTotalCount() const noexcept -> size_t { return m_Models.size(); }
  /// Moves the result out only after full completion or cancellation.
  /// @return Complete variants or explicit cancellation; errors if still running, failed, or already taken.
  auto TakeResult() -> Engine::Result<BrdfAnalysisCaptureResult>;

private:
  BrdfAnalysisCaptureSession(BrdfAnalysisSnapshot snapshot, std::vector<BrdfModel> models);
  void Fail() noexcept;

  BrdfAnalysisSnapshot m_Snapshot;
  std::vector<BrdfModel> m_Models;
  std::vector<BrdfAnalysisVariant> m_Variants;
  std::optional<BrdfAnalysisRenderRequest> m_CurrentRequest;
  BrdfAnalysisCaptureSessionState m_Status = BrdfAnalysisCaptureSessionState::Ready;
  bool m_ResultTaken = false;
};

/// Builds and renders comparable variants from one captured scene snapshot.
/// Each request owns a scene-document copy and changes only the selected
/// material's BRDF model plus explicit frame-local overrides. Calls are ordered,
/// stop at the first error, and never return partial results. The render callback
/// is responsible for rendering the supplied request using the engine's normal
/// asset/render setup.
/// @param snapshot Captured scene, material, camera, and render settings.
/// @param models Unique supported BRDF models to compare.
/// @param render Renders one request and returns its final output.
/// @return All rendered variants, or an error if inputs or any render fail.
ENGINE_API auto ExecuteBrdfAnalysisVariants(const BrdfAnalysisSnapshot& snapshot, std::span<const BrdfModel> models,
                                            const BrdfAnalysisRenderCallback& render)
    -> Engine::Result<std::vector<BrdfAnalysisVariant>>;

/// Renders complete, sequential variants with frame-local overrides and cancellation.
/// Cancellation, callback failure, malformed output, or a thrown callback discards
/// all accumulated variants. Progress is reported after each fully validated render.
/// @param snapshot Owned scene/material/camera/render inputs and optional frame overrides.
/// @param models Unique implemented BRDF models to render.
/// @param render Renders one request and returns all five CPU-owned AOV planes.
/// @param options Progress and cancellation callbacks.
/// @return A complete capture or an explicit cancelled result; errors never expose partial output.
ENGINE_API auto CaptureBrdfAnalysisVariants(const BrdfAnalysisSnapshot& snapshot, std::span<const BrdfModel> models,
                                            const BrdfAnalysisRenderCallback& render,
                                            const BrdfAnalysisCaptureOptions& options = {})
    -> Engine::Result<BrdfAnalysisCaptureResult>;

} // namespace Engine::Renderer::BrdfAnalysis

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
