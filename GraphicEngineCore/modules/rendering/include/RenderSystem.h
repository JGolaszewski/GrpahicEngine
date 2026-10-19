#pragma once
#include "assets/include/AssetImporter.h"
#include "rendering/include/PrimitiveFactory.h"
#include "rendering/include/RenderBackend.h"
#include "rendering/include/RenderLimits.h"
#include "rendering/include/RenderOutput.h"
#include "rendering/include/SnapshotFrame.h"
#include "rendering/include/ViewportTarget.h"
#include "rendering/include/renderer/CameraView.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/include/renderer/ViewportBinding.h"
#include "runtime/include/CoreExport.h"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine {
class AssetManager;
class SceneRenderer;
namespace Renderer {
class MaterialSystem;
class Material;
class Mesh;
struct TextureResource;
struct Submesh;
struct Vertex;
struct MaterialProperties;
class ModelResourceLibrary;
struct FrameDiagnostics;
struct GpuFrameTiming;
class ResourceManager;
class ShaderFactory;
struct DisplayTransformResources;
class FrameContext;
class RenderGraph;
} // namespace Renderer
/// Configuration limits for the shadow rendering pass.
struct ShadowSettings {
  uint32_t Resolution = Engine::ShadowMapSize;                   ///< Shadow-map resolution in pixels.
  uint32_t MaxShadowCastersPerScene = Engine::MaxShadowedLights; ///< Maximum shadow-casting lights per scene.
};

/// Coordinates frame rendering, resource management, and render graph execution.
class ENGINE_API RenderSystem {
public:
  /// Opaque identifier for an asynchronous final-render readback request.
  using FinalRenderTicket = uint64_t;
  /// Opaque identifier for an asynchronous snapshot-frame capture.
  using SnapshotRenderTicket = FinalRenderTicket;
  /// Snapshot capture progress and terminal diagnostic.
  struct SnapshotRenderStatus {
    enum class Phase : uint8_t { WaitingForReadback, Completed, Failed, Canceled };
    Phase CurrentPhase = Phase::WaitingForReadback;
    float Progress = 0.0F;
    std::string Message;
  };
  /// Create a render system using the supplied backend.
  /// @param backend Backend that owns the device and command list.
  RenderSystem(IRenderBackend* backend);
  ~RenderSystem();
  /// Return whether the backend command list is ready for rendering.
  /// @return True when the backend is usable.
  bool IsValid() const;
  /// Supplies the project asset manager used to resolve authored material texture identities.
  /// @param assets Asset manager that remains alive for this render system's lifetime.
  void SetAssetManager(AssetManager* assets);
  /// Resolves and uploads an authored project texture before frame rendering begins.
  /// @param guid Stable texture asset identity.
  /// @param srgb Whether the texture should use sRGB sampling.
  /// @param error Receives a missing asset or upload diagnostic.
  /// @return True when the texture resource is ready for scene material bindings.
  bool PrepareAssetTexture(const std::string& guid, bool srgb, std::string& error);
  /// Returns a previously prepared authored project texture.
  /// @param guid Stable texture asset identity.
  /// @param srgb Whether the texture uses sRGB sampling.
  /// @return Prepared GPU resource, or empty when it has not been prepared.
  std::shared_ptr<::Engine::Renderer::TextureResource> GetPreparedAssetTexture(const std::string& guid,
                                                                               bool srgb) const;

  /// Begin frame recording before submitting view work.
  void BeginFrame();
  /// Submit scene and shadow views for the active frame.
  /// @param views Views to render in the active frame.
  void SubmitViews(const std::vector<Engine::ViewportBinding>& views);
  /// Finish and submit the active frame to the backend.
  void EndFrame();
  /// Replaces the shadow rendering limits.
  /// @param settings New shadow settings.
  /// @return True when the settings are valid and accepted.
  bool SetShadowSettings(const ShadowSettings& settings) noexcept;
  /// Gets the active shadow settings.
  /// @return Read-only reference to the current settings.
  const ShadowSettings& GetShadowSettings() const noexcept { return m_ShadowSettings; }
  /// Picks the light nearest to a viewport location.
  /// @param viewportX Horizontal viewport coordinate.
  /// @param viewportY Vertical viewport coordinate.
  /// @return Picked light identity, or no value when no light is hit.
  std::optional<Engine::LightId> PickLight(float viewportX, float viewportY) const;
  /// Creates a render target for a viewport.
  /// @param width Target width in pixels.
  /// @param height Target height in pixels.
  /// @return New target, or no value when creation fails.
  std::shared_ptr<Engine::ViewportTarget> CreateViewportTarget(uint32_t width, uint32_t height);
  /// Submit a scene-camera render and GPU-to-staging copies without waiting for
  /// the GPU. PollFinalRender must be called on the render thread until it
  /// returns an output or an error. Only one final-render ticket may be active.
  /// The returned RenderOutput is a transient set of five float AOVs; it is
  /// not by itself an immutable Snapshot Analysis frame or its metadata.
  Engine::Result<FinalRenderTicket> BeginFinalRender(Engine::Scene& scene, const ::Engine::Renderer::CameraView& camera,
                                                     uint32_t width, uint32_t height,
                                                     const Engine::FinalColorPostProcess& postProcess = {});
  /// Captures one complete immutable analysis frame. Scene packet construction and input/resource capture happen
  /// synchronously before this function returns; polling only waits for GPU readback and never revisits the scene.
  /// Selected planes are retained in the immutable frame; an empty or unsupported mask is rejected.
  Engine::Result<SnapshotRenderTicket> BeginSnapshotRender(Engine::Scene& scene,
                                                           const ::Engine::Renderer::CameraView& camera,
                                                           SnapshotFrameMetadata metadata, uint32_t width,
                                                           uint32_t height,
                                                           SnapshotPlaneMask selectedPlanes = AllSnapshotPlanes);
  /// Polls a snapshot request. An empty optional means GPU work is still pending; no partial frame is returned.
  /// A completed frame is returned once; repeated polls report an error without changing the completed status.
  Engine::Result<std::optional<SnapshotFrame>> PollSnapshotRender(SnapshotRenderTicket ticket);
  /// Returns the current progress or terminal result for a snapshot request. Status remains available until the next
  /// snapshot request begins.
  std::optional<SnapshotRenderStatus> GetSnapshotRenderStatus(SnapshotRenderTicket ticket) const;
  /// Poll a submitted final render without blocking. An empty optional means
  /// the GPU copy has not completed; the ticket is consumed on output or error.
  Engine::Result<std::optional<Engine::RenderOutput>> PollFinalRender(FinalRenderTicket ticket);
  /// Cancel a pending final render and release its GPU resources. This may wait
  /// for submitted GPU work and is intended for orderly service shutdown.
  Engine::Status CancelFinalRender(FinalRenderTicket ticket);
  /// Render the scene through its supplied scene camera into offscreen AOVs.
  /// The blocking compatibility wrapper records and submits a complete frame
  /// and returns CPU-owned image planes; asynchronous consumers should use the
  /// ticket-based API above.
  /// @param scene Scene to render.
  /// @param camera Resolved camera parameters.
  /// @param width Output width in pixels.
  /// @param height Output height in pixels.
  /// @param postProcess Optional in-place final-color hook.
  /// @return CPU-owned AOV output, or an error when rendering or readback fails.
  Engine::Result<Engine::RenderOutput> RenderFinal(Engine::Scene& scene, const ::Engine::Renderer::CameraView& camera,
                                                   uint32_t width, uint32_t height,
                                                   const Engine::FinalColorPostProcess& postProcess = {});
  /// Creates a built-in primitive and registers it for scene rendering.
  /// @param shape Primitive shape to create.
  /// @param material Material applied to the primitive.
  /// @return True when the primitive was created and uploaded.
  bool CreatePrimitive(Engine::PrimitiveShape shape, std::shared_ptr<::Engine::Renderer::Material> material);

  /// Finds or uploads a mesh resource for an imported asset generation.
  /// @param guid Stable model identity.
  /// @param generation Asset generation used to invalidate stale resources.
  /// @param vertices Vertex data to upload when no matching resource exists.
  /// @param indices Triangle index data to upload.
  /// @param error Receives a failure description when creation fails.
  /// @return GPU mesh, or an empty pointer on failure.
  std::shared_ptr<::Engine::Renderer::Mesh> GetOrCreateMesh(const std::string& guid, uint64_t generation,
                                                            const std::vector<::Engine::Renderer::Vertex>& vertices,
                                                            const std::vector<uint32_t>& indices, std::string& error);
  /// Finds or uploads a texture resource for an imported asset generation.
  /// @param identity Stable texture identity.
  /// @param generation Asset generation used to invalidate stale resources.
  /// @param image Decoded image pixels to upload.
  /// @param srgb Whether the texture uses sRGB sampling.
  /// @param error Receives a failure description when creation fails.
  /// @param sampler Optional sampler settings from the imported texture.
  /// @return GPU texture resource, or an empty pointer on failure.
  std::shared_ptr<::Engine::Renderer::TextureResource>
  GetOrCreateTexture(const std::string& identity, uint64_t generation, const ImportedImage& image, bool srgb,
                     std::string& error, const ImportedTexture* sampler = nullptr);
  /// Creates a material from renderer properties.
  /// @param properties Material parameters and texture transforms.
  /// @return New material, or an empty pointer on failure.
  std::shared_ptr<::Engine::Renderer::Material>
  CreateMaterial(const ::Engine::Renderer::MaterialProperties& properties);
  /// Releases a material from the renderer's resource manager.
  /// @param material Material to unregister.
  void UnregisterMaterial(const std::shared_ptr<::Engine::Renderer::Material>& material);
  /// Assigns base-color and normal textures to a material.
  /// @param material Material to update.
  /// @param baseColor Base-color texture resource.
  /// @param normal Normal-map texture resource.
  /// @param baseColorGuid Optional identity to associate with the base-color texture.
  /// @param normalGuid Optional identity to associate with the normal texture.
  /// @param setImportedDefaults Whether imported defaults should replace absent textures.
  /// @return True when the texture bindings were accepted.
  bool SetMaterialTextures(const std::shared_ptr<::Engine::Renderer::Material>& material,
                           std::shared_ptr<::Engine::Renderer::TextureResource> baseColor,
                           std::shared_ptr<::Engine::Renderer::TextureResource> normal,
                           std::optional<std::string> baseColorGuid = {}, std::optional<std::string> normalGuid = {},
                           bool setImportedDefaults = false);
  /// Assigns all supported PBR texture maps to a material.
  /// @param material Material to update.
  /// @param baseColor Base-color texture resource.
  /// @param normal Normal-map texture resource.
  /// @param metallicRoughness Combined metallic-roughness texture resource.
  /// @param occlusion Ambient-occlusion texture resource.
  /// @param emissive Emissive texture resource.
  /// @param setImportedDefaults Whether imported defaults should replace absent textures.
  /// @return True when the texture bindings were accepted.
  bool SetPbrMaterialTextures(const std::shared_ptr<::Engine::Renderer::Material>& material,
                              std::shared_ptr<::Engine::Renderer::TextureResource> baseColor,
                              std::shared_ptr<::Engine::Renderer::TextureResource> normal,
                              std::shared_ptr<::Engine::Renderer::TextureResource> metallicRoughness,
                              std::shared_ptr<::Engine::Renderer::TextureResource> occlusion,
                              std::shared_ptr<::Engine::Renderer::TextureResource> emissive,
                              bool setImportedDefaults = false);
  /// Releases resources associated with an imported asset identity.
  /// @param guid Stable asset identity.
  void ReleaseResource(const std::string& guid);
  /// Checks whether a frame is currently being recorded.
  /// @return True between BeginFrame and EndFrame.
  bool IsFrameActive() const noexcept { return m_FrameActive; }
  /// Uploads an imported model and returns its mesh resources by source submesh.
  /// @param guid Stable model identity.
  /// @param generation Asset generation used to invalidate stale resources.
  /// @param model Imported model data.
  /// @param meshes Receives uploaded meshes grouped by imported node and primitive.
  /// @param error Receives a failure description when upload fails.
  /// @return True when all model resources were uploaded.
  bool UploadModel(const std::string& guid, uint64_t generation, const ImportedModel& model,
                   std::vector<std::vector<std::shared_ptr<::Engine::Renderer::Mesh>>>& meshes, std::string& error);
  /// Prepares renderer resources for an imported model and its material overrides.
  /// @param guid Stable model identity.
  /// @param generation Asset generation used to invalidate stale resources.
  /// @param model Imported model data.
  /// @param materialOverrides Optional per-material overrides.
  /// @param error Receives a failure description when preparation fails.
  /// @return True when all required resources were prepared.
  bool
  PrepareModelResources(const std::string& guid, uint64_t generation, const ImportedModel& model,
                        const std::vector<std::optional<::Engine::Renderer::MaterialProperties>>& materialOverrides,
                        std::string& error);

  /// Gets the scene renderer owned by this system.
  /// @return Scene renderer pointer, or null when unavailable.
  SceneRenderer* GetSceneRenderer() const { return m_SceneRenderer.get(); }
  /// Gets the model resource library.
  /// @return Read-only model-resource library.
  const ::Engine::Renderer::ModelResourceLibrary& GetModelResources() const noexcept;
  /// Gets diagnostics from the most recently submitted frame.
  /// @return Read-only frame diagnostics.
  const ::Engine::Renderer::FrameDiagnostics& GetFrameDiagnostics() const noexcept;
  /// Gets GPU timing data for the most recently submitted frame.
  /// @return Frame timing record.
  ::Engine::Renderer::GpuFrameTiming GetGpuFrameTiming() const noexcept;
  /// Gets the number of views submitted in the active or previous frame.
  /// @return Submitted view count.
  uint32_t GetSubmittedViewCount() const noexcept;
  /// Gets the shader factory owned by this system.
  /// @return Shader factory pointer, or null when unavailable.
  ::Engine::Renderer::ShaderFactory* GetShaderFactory() const { return m_ShaderFactory.get(); }
  /// Gets the render graph owned by this system.
  /// @return Render graph pointer, or null when unavailable.
  ::Engine::Renderer::RenderGraph* GetRenderGraph() const { return m_RenderGraph.get(); }
  /// Gets the primitive factory owned by this system.
  /// @return Primitive factory pointer, or null when unavailable.
  Engine::PrimitiveFactory* GetPrimitiveFactory() const { return m_PrimitiveFactory.get(); }
  /// Gets the active rendering backend.
  /// @return Backend pointer supplied at construction.
  IRenderBackend* GetRenderBackend() const { return m_RenderBackend; }

private:
  struct CommandListHolder;
  struct FinalReadbackCapture;
  struct CompletedSnapshotCapture;
  struct PendingModelSubmesh {
    size_t MeshIndex = 0;
    size_t PrimitiveIndex = 0;
    int32_t MaterialIndex = -1;
    DirectX::XMFLOAT4X4 LocalTransform{};
    DirectX::XMFLOAT3 BoundsMin{};
    DirectX::XMFLOAT3 BoundsMax{};
  };
  static bool CollectModelSubmeshes(const ImportedModel& model, std::vector<PendingModelSubmesh>& pending,
                                    std::string& error);
  bool CreateModelMaterials(const std::string& guid, uint64_t generation, const ImportedModel& model,
                            const std::vector<std::optional<::Engine::Renderer::MaterialProperties>>& overrides,
                            std::vector<std::shared_ptr<::Engine::Renderer::Material>>& materials, std::string& error);
  auto AssembleModelSubmeshes(const std::vector<std::vector<std::shared_ptr<::Engine::Renderer::Mesh>>>& meshes,
                              const std::vector<PendingModelSubmesh>& pending,
                              const std::vector<std::shared_ptr<::Engine::Renderer::Material>>& materials,
                              std::string& error) -> std::optional<std::vector<::Engine::Renderer::Submesh>>;
  bool CreateImportedMaterial(const std::string& guid, uint64_t generation, const ImportedModel& model,
                              size_t materialIndex,
                              const std::vector<std::optional<::Engine::Renderer::MaterialProperties>>& overrides,
                              std::shared_ptr<::Engine::Renderer::Material>& material, std::string& error);
  bool ResolveImportedTexture(const std::string& guid, uint64_t generation, const ImportedModel& model,
                              int32_t textureIndex, bool srgb,
                              std::shared_ptr<::Engine::Renderer::TextureResource>& destination, std::string& error);
  void UnregisterMaterials(const std::vector<std::shared_ptr<::Engine::Renderer::Material>>& materials);
  void BeginFrameInternal(bool clearDefaultFramebuffer);
  void ClearFrameBuffer();
  void ClearSubmittedTargets();
  Engine::Result<std::optional<Engine::RenderOutput>> PollFinalRenderOutput(FinalRenderTicket ticket);

  IRenderBackend* const m_RenderBackend;
  std::unique_ptr<CommandListHolder> m_CommandList;
  std::unique_ptr<FinalReadbackCapture> m_FinalReadbackCapture;
  FinalRenderTicket m_NextFinalRenderTicket = 0;
  std::optional<SnapshotFrameMetadata> m_StartingSnapshotMetadata;
  SnapshotPlaneMask m_StartingSnapshotSelectedPlanes = AllSnapshotPlanes;
  std::unique_ptr<CompletedSnapshotCapture> m_CompletedSnapshotCapture;
  std::map<SnapshotRenderTicket, SnapshotRenderStatus> m_SnapshotStatuses;
  bool m_FrameActive = false;
  std::string m_LastUploadFailure;
  std::string m_LastRenderGraphFailure;
  ShadowSettings m_ShadowSettings;
  AssetManager* m_AssetManager = nullptr;
  std::map<std::pair<std::string, bool>, std::shared_ptr<::Engine::Renderer::TextureResource>> m_AuthoredTextures;

  std::unique_ptr<::Engine::Renderer::FrameContext> m_FrameContext;
  std::unique_ptr<::Engine::Renderer::RenderGraph> m_RenderGraph;

  std::unique_ptr<SceneRenderer> m_SceneRenderer;
  std::unique_ptr<::Engine::Renderer::MaterialSystem> m_MaterialSystem;
  std::unique_ptr<::Engine::Renderer::ModelResourceLibrary> m_ModelResources;
  std::unique_ptr<::Engine::Renderer::ResourceManager> m_ResourceManager;
  std::unique_ptr<::Engine::Renderer::ShaderFactory> m_ShaderFactory;
  std::shared_ptr<::Engine::Renderer::DisplayTransformResources> m_DisplayTransformResources;
  std::unique_ptr<Engine::PrimitiveFactory> m_PrimitiveFactory;
};
} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
