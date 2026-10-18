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

/// Immutable-by-convention inputs captured for repeatable BRDF comparison.
/// The owned scene document and material values are copies; analysis variants
/// are derived from them without changing authored scene or asset state.
struct ENGINE_API BrdfAnalysisSnapshot {
  Engine::SceneDocument Scene;                  ///< Complete authored scene state at capture time.
  uint64_t TargetEntityId = 0;                  ///< Document-local id of the entity containing the selected material.
  std::optional<size_t> MaterialOverrideIndex;  ///< Per-submesh override slot, or empty for the entity material.
  Engine::SceneMaterialDocument SourceMaterial; ///< Exact serialized material data, including texture identities.
  MaterialProperties SourceMaterialProperties;  ///< Resolved numeric factors used by CPU BRDF measurements.
  CameraView Camera;                            ///< Resolved world-space camera pose and projection.
  uint32_t Width = 0;                           ///< Render width captured for every comparison variant.
  uint32_t Height = 0;                          ///< Render height captured for every comparison variant.
  Engine::FinalColorPostProcess PostProcess;    ///< Final-color processing captured for every variant.
};

/// Complete immutable request for rendering one BRDF model from a snapshot.
struct ENGINE_API BrdfAnalysisRenderRequest {
  Engine::SceneDocument Scene;               ///< Per-variant copy of the captured scene.
  BrdfModel Model = BrdfModel::Legacy;       ///< BRDF model selected for this variant.
  MaterialProperties Material;               ///< Captured material factors with only Model changed.
  CameraView Camera;                         ///< Captured camera settings shared by all variants.
  uint32_t Width = 0;                        ///< Captured render width.
  uint32_t Height = 0;                       ///< Captured render height.
  Engine::FinalColorPostProcess PostProcess; ///< Captured final-color processing.
};

/// Render request and output for one completed comparison variant.
struct ENGINE_API BrdfAnalysisVariant {
  BrdfAnalysisRenderRequest Request; ///< Inputs used to produce this output.
  Engine::RenderOutput Output;       ///< CPU-owned render result for scene luminance measurement and display.
};

/// Renders the supplied immutable request and returns its CPU-owned output.
using BrdfAnalysisRenderCallback =
    std::function<Engine::Result<Engine::RenderOutput>(const BrdfAnalysisRenderRequest& request)>;

/// Builds and renders comparable variants from one captured scene snapshot.
/// Each request owns a scene-document copy and changes only the selected
/// material's BRDF model. Calls are ordered, stop at the first error, and never
/// return partial results. The render callback is responsible for rendering
/// the supplied request using the engine's normal asset/render setup.
/// @param snapshot Captured scene, material, camera, and render settings.
/// @param models Unique supported BRDF models to compare.
/// @param render Renders one request and returns its final output.
/// @return All rendered variants, or an error if inputs or any render fail.
ENGINE_API auto ExecuteBrdfAnalysisVariants(const BrdfAnalysisSnapshot& snapshot, std::span<const BrdfModel> models,
                                            const BrdfAnalysisRenderCallback& render)
    -> Engine::Result<std::vector<BrdfAnalysisVariant>>;

} // namespace Engine::Renderer::BrdfAnalysis

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
