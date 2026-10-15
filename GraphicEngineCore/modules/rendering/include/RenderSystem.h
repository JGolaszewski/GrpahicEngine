#pragma once
#include "assets/include/AssetImporter.h"
#include "rendering/include/PrimitiveFactory.h"
#include "rendering/include/RenderBackend.h"
#include "rendering/include/RenderLimits.h"
#include "rendering/include/RenderOutput.h"
#include "rendering/include/ViewportTarget.h"
#include "rendering/include/renderer/CameraView.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/include/renderer/ViewportBinding.h"
#include "runtime/include/CoreExport.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine {
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
class FrameContext;
class RenderGraph;
} // namespace Renderer
/// Configuration limits for the shadow rendering pass.
struct ShadowSettings {
  uint32_t Resolution = Engine::ShadowMapSize;
  uint32_t MaxShadowCastersPerScene = Engine::MaxShadowedLights;
};

/// Coordinates frame rendering, resource management, and render graph execution.
class ENGINE_API RenderSystem {
public:
  /// Opaque identifier for an asynchronous final-render readback request.
  using FinalRenderTicket = uint64_t;
  /// Create a render system using the supplied backend.
  RenderSystem(IRenderBackend* backend);
  ~RenderSystem();
  /// Return whether the backend command list is ready for rendering.
  bool IsValid() const;

  /// Begin frame recording before submitting view work.
  void BeginFrame();
  /// Submit scene and shadow views for the active frame.
  void SubmitViews(const std::vector<Engine::ViewportBinding>& views);
  /// Finish and submit the active frame to the backend.
  void EndFrame();
  bool SetShadowSettings(const ShadowSettings& settings) noexcept;
  const ShadowSettings& GetShadowSettings() const noexcept { return m_ShadowSettings; }
  std::optional<Engine::LightId> PickLight(float viewportX, float viewportY) const;
  std::shared_ptr<Engine::ViewportTarget> CreateViewportTarget(uint32_t width, uint32_t height);
  /// Submit a scene-camera render and GPU-to-staging copies without waiting for
  /// the GPU. PollFinalRender must be called on the render thread until it
  /// returns an output or an error. Only one final-render ticket may be active.
  Engine::Result<FinalRenderTicket> BeginFinalRender(Engine::Scene& scene, const ::Engine::Renderer::CameraView& camera,
                                                     uint32_t width, uint32_t height,
                                                     const Engine::FinalColorPostProcess& postProcess = {});
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
  Engine::Result<Engine::RenderOutput> RenderFinal(Engine::Scene& scene, const ::Engine::Renderer::CameraView& camera,
                                                   uint32_t width, uint32_t height,
                                                   const Engine::FinalColorPostProcess& postProcess = {});
  bool CreatePrimitive(Engine::PrimitiveShape shape, std::shared_ptr<::Engine::Renderer::Material> material);

  std::shared_ptr<::Engine::Renderer::Mesh> GetOrCreateMesh(const std::string& guid, uint64_t generation,
                                                            const std::vector<::Engine::Renderer::Vertex>& vertices,
                                                            const std::vector<uint32_t>& indices, std::string& error);
  std::shared_ptr<::Engine::Renderer::TextureResource>
  GetOrCreateTexture(const std::string& identity, uint64_t generation, const ImportedImage& image, bool srgb,
                     std::string& error, const ImportedTexture* sampler = nullptr);
  std::shared_ptr<::Engine::Renderer::Material>
  CreateMaterial(const ::Engine::Renderer::MaterialProperties& properties);
  void UnregisterMaterial(const std::shared_ptr<::Engine::Renderer::Material>& material);
  bool SetMaterialTextures(const std::shared_ptr<::Engine::Renderer::Material>& material,
                           std::shared_ptr<::Engine::Renderer::TextureResource> baseColor,
                           std::shared_ptr<::Engine::Renderer::TextureResource> normal,
                           std::optional<std::string> baseColorGuid = {}, std::optional<std::string> normalGuid = {},
                           bool setImportedDefaults = false);
  bool SetPbrMaterialTextures(const std::shared_ptr<::Engine::Renderer::Material>& material,
                              std::shared_ptr<::Engine::Renderer::TextureResource> baseColor,
                              std::shared_ptr<::Engine::Renderer::TextureResource> normal,
                              std::shared_ptr<::Engine::Renderer::TextureResource> metallicRoughness,
                              std::shared_ptr<::Engine::Renderer::TextureResource> occlusion,
                              std::shared_ptr<::Engine::Renderer::TextureResource> emissive,
                              bool setImportedDefaults = false);
  void ReleaseResource(const std::string& guid);
  bool IsFrameActive() const noexcept { return m_FrameActive; }
  bool UploadModel(const std::string& guid, uint64_t generation, const ImportedModel& model,
                   std::vector<std::vector<std::shared_ptr<::Engine::Renderer::Mesh>>>& meshes, std::string& error);
  bool
  PrepareModelResources(const std::string& guid, uint64_t generation, const ImportedModel& model,
                        const std::vector<std::optional<::Engine::Renderer::MaterialProperties>>& materialOverrides,
                        std::string& error);

  SceneRenderer* GetSceneRenderer() const { return m_SceneRenderer.get(); }
  const ::Engine::Renderer::ModelResourceLibrary& GetModelResources() const noexcept;
  const ::Engine::Renderer::FrameDiagnostics& GetFrameDiagnostics() const noexcept;
  ::Engine::Renderer::GpuFrameTiming GetGpuFrameTiming() const noexcept;
  uint32_t GetSubmittedViewCount() const noexcept;
  ::Engine::Renderer::ShaderFactory* GetShaderFactory() const { return m_ShaderFactory.get(); }
  ::Engine::Renderer::RenderGraph* GetRenderGraph() const { return m_RenderGraph.get(); }
  Engine::PrimitiveFactory* GetPrimitiveFactory() const { return m_PrimitiveFactory.get(); }
  IRenderBackend* GetRenderBackend() const { return m_RenderBackend; }

private:
  struct CommandListHolder;
  struct FinalReadbackCapture;
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
  void ClearFrameBuffer();
  void ClearSubmittedTargets();

  IRenderBackend* const m_RenderBackend;
  std::unique_ptr<CommandListHolder> m_CommandList;
  std::unique_ptr<FinalReadbackCapture> m_FinalReadbackCapture;
  FinalRenderTicket m_NextFinalRenderTicket = 0;
  bool m_FrameActive = false;
  std::string m_LastUploadFailure;
  std::string m_LastRenderGraphFailure;
  ShadowSettings m_ShadowSettings;

  std::unique_ptr<::Engine::Renderer::FrameContext> m_FrameContext;
  std::unique_ptr<::Engine::Renderer::RenderGraph> m_RenderGraph;

  std::unique_ptr<SceneRenderer> m_SceneRenderer;
  std::unique_ptr<::Engine::Renderer::MaterialSystem> m_MaterialSystem;
  std::unique_ptr<::Engine::Renderer::ModelResourceLibrary> m_ModelResources;
  std::unique_ptr<::Engine::Renderer::ResourceManager> m_ResourceManager;
  std::unique_ptr<::Engine::Renderer::ShaderFactory> m_ShaderFactory;
  std::unique_ptr<Engine::PrimitiveFactory> m_PrimitiveFactory;
};
} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
