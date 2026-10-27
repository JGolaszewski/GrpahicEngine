#pragma once

#include "runtime/include/CoreExport.h"
#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nvrhi/nvrhi.h>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "rendering/include/renderer/DebugGeometry.h"
#include "rendering/include/renderer/Light.h"
#include "rendering/include/renderer/ModelResourceLibrary.h"
#include "rendering/include/renderer/RenderResources.h"
#include "rendering/include/renderer/ShadowDiagnostics.h"
#include "rendering/src/EnvironmentLighting.h"
#include "rendering/src/MaterialSystem.h"
#include "rendering/src/PipelineCache.h"
#include "rendering/src/RenderPacket.h"
#include "rendering/src/ShaderFactory.h"
#include "rendering/src/render_graph/FrameBlackboard.h"
#include "rendering/src/render_graph/FrameContext.h"
#include "rendering/src/render_graph/RenderView.h"
#include "scene/include/Scene.h"

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine {

class ENGINE_API SceneRenderer {
public:
  static constexpr uint32_t ShadowMapSize = 2048;
  static constexpr uint32_t MaxLights = ENGINE_MAX_LIGHTS;
  static constexpr uint32_t MaxShadowedLights = 4;
  static constexpr uint32_t PerObjectDataStride = 256;

  struct SubmissionStats {
    uint32_t ObjectsSubmitted = 0;
    uint32_t ObjectsCulled = 0;
    uint32_t PacketsBuilt = 0;
    uint32_t OpaquePackets = 0;
    uint32_t MaskedPackets = 0;
    uint32_t TransparentPackets = 0;
    uint32_t PipelineCacheHits = 0;
    uint32_t PipelineCacheMisses = 0;
    uint32_t PipelineCreations = 0;
    uint32_t PerObjectBytesUsed = 0;
    double CullingTimeMs = 0.0;
    double PacketBuildTimeMs = 0.0;
  };

  SceneRenderer(nvrhi::IDevice* device);
  void Init(nvrhi::IDevice* device, ::Engine::Renderer::MaterialSystem* materialSystem,
            ::Engine::Renderer::ShaderFactory* shaderFactory,
            const ::Engine::Renderer::ModelResourceLibrary* modelResources);
  void SetAssetTextureResolver(
      std::function<std::shared_ptr<::Engine::Renderer::TextureResource>(const std::string&, bool, bool, bool, float)>
          resolver);

  void BuildViews(::Engine::Renderer::FrameBlackboard& blackboard, nvrhi::ICommandList* commandList,
                  ::Engine::Renderer::FrameContext& frameContext, ::Engine::Renderer::FrameDiagnostics& diagnostics);
  static void BuildDebugGeometry(::Engine::Renderer::RenderView* view);
  void RenderDebugGeometry(nvrhi::ICommandList* commandList, const ::Engine::Renderer::RenderView& view);
  static std::optional<Engine::LightId> PickLight(const ::Engine::Renderer::RenderView& view, float viewportX,
                                                  float viewportY);
  static void BuildLighting(Engine::Scene* scene, const std::vector<::Engine::Renderer::RenderView*>& shadowViews,
                            std::vector<::Engine::Renderer::DirectionalLightData>& lights);
  static void BuildShadowData(Engine::Scene* scene, const std::vector<::Engine::Renderer::RenderView*>& shadowViews,
                              uint32_t firstShadowSlot, uint32_t maxShadowedLights,
                              const ::Engine::Renderer::ModelResourceLibrary* modelResources = nullptr,
                              const std::vector<::Engine::Renderer::RenderView*>& sceneViews = {},
                              uint32_t shadowMapResolution = 2048,
                              std::vector<::Engine::Renderer::DirectionalShadowDiagnostics>* diagnostics = nullptr);
  static uint32_t CountShadowCasters(const Engine::Scene* scene, uint32_t limit);
  /// Counts depth views needed by the selected shadow-casting lights (point lights use six faces).
  /// @param scene Scene whose light components are counted.
  /// @param casterLimit Maximum number of shadow-casting lights to include.
  /// @return Number of shadow-map array slices required.
  static uint32_t CountShadowViews(const Engine::Scene* scene, uint32_t casterLimit);

  static bool EnsureLightBuffers(size_t viewCount, ::Engine::Renderer::FrameContext& frameContext);
  static nvrhi::BufferHandle GetLightBuffer(size_t viewIndex, ::Engine::Renderer::FrameContext& frameContext);
  nvrhi::BindingSetHandle GetSceneBindingSet(size_t viewIndex, nvrhi::BufferHandle lightBuffer,
                                             nvrhi::TextureHandle shadowMap, nvrhi::SamplerHandle shadowSampler,
                                             nvrhi::BufferHandle cameraBuffer, nvrhi::TextureHandle ambientOcclusion,
                                             const ::Engine::Renderer::EnvironmentLightingResources* environmentSource,
                                             ::Engine::Renderer::FrameDiagnostics& diagnostics);
  bool DrawEnvironmentBackground(nvrhi::ICommandList* commandList, const ::Engine::Renderer::RenderView& view);
  inline nvrhi::BufferHandle GetPerObjectBuffer() const { return m_PerObjectBuffer; }
  inline nvrhi::BindingSetHandle GetPerObjectBindingSet() const { return m_PerObjectBindingSet; }
  inline nvrhi::SamplerHandle GetShadowSampler() const { return m_ShadowSampler; }
  inline nvrhi::ShaderHandle GetAmbientOcclusionPrepassPS() const { return m_AmbientOcclusionPrepassPS; }
  inline nvrhi::BindingLayoutHandle GetPerObjectBindingLayout() const { return m_PerObjectBindingLayout; }
  inline nvrhi::BindingLayoutHandle GetSceneBindingLayout() const { return m_SceneBindingLayout; }
  inline ::Engine::Renderer::MaterialSystem* GetMaterialSystem() const { return m_MaterialSystem; }
  inline PipelineCache* GetPipelineCache() { return &m_PipelineCache; }
  inline const SubmissionStats& GetSubmissionStats() const { return m_SubmissionStats; }

private:
  void CullAndFillView(::Engine::Renderer::RenderView* view);
  void SubmitSubmesh(::Engine::Renderer::RenderView* view, const ::Engine::Ecs::WorldTransform& transform,
                     ::Engine::EntityHandle entity, const ::Engine::Ecs::MeshRenderer& meshRenderer,
                     const ::Engine::Renderer::Submesh& submesh, DirectX::FXMMATRIX viewProjection,
                     const nvrhi::FramebufferInfoEx& framebufferInfo);
  auto MaterialForSubmesh(const ::Engine::Renderer::RenderView* view, ::Engine::EntityHandle entity,
                          const ::Engine::Ecs::MeshRenderer& meshRenderer, const ::Engine::Renderer::Submesh& submesh)
      -> std::shared_ptr<::Engine::Renderer::Material>;
  bool IsSubmeshVisible(::Engine::Renderer::RenderView* view, const ::Engine::Renderer::Submesh& submesh,
                        DirectX::FXMMATRIX world, const DirectX::XMFLOAT4X4& worldTransform);
  bool ConfigureAndQueuePacket(::Engine::Renderer::RenderView* view, const ::Engine::Ecs::MeshRenderer& meshRenderer,
                               const ::Engine::Renderer::Submesh& submesh,
                               const ::Engine::Renderer::MaterialProperties& materialProperties,
                               bool hasTextureOverrides, const nvrhi::FramebufferInfoEx& framebufferInfo,
                               ::Engine::Renderer::RenderPacket& packet, bool disableFaceCulling);
  static void SortViewQueues(::Engine::Renderer::RenderView* view);
  void EnsureDebugBuffer(nvrhi::BufferHandle& buffer, uint64_t& capacity, size_t vertexCount, const char* debugName);
  nvrhi::GraphicsPipelineHandle GetDebugPipeline(const nvrhi::FramebufferInfo& framebuffer,
                                                 nvrhi::PrimitiveType primitiveType,
                                                 ::Engine::Renderer::DebugDepthMode depthMode);
  bool EnsurePerObjectResources(size_t maxDrawCount, ::Engine::Renderer::FrameContext& frameContext,
                                ::Engine::Renderer::FrameDiagnostics& diagnostics);
  size_t EstimateDrawCount(const ::Engine::Renderer::FrameBlackboard& blackboard) const;
  static ::Engine::Renderer::RenderPacket CreateRenderPacket(const ::Engine::Renderer::Mesh* mesh,
                                                             const ::Engine::Renderer::Material* material);

  PipelineCache m_PipelineCache;

  nvrhi::DeviceHandle m_Device;
  struct SceneBindingCache {
    nvrhi::BufferHandle LightBuffer;
    nvrhi::TextureHandle ShadowMap;
    nvrhi::SamplerHandle ShadowSampler;
    nvrhi::BufferHandle CameraBuffer;
    nvrhi::TextureHandle AmbientOcclusion;
    nvrhi::TextureHandle IrradianceMap;
    nvrhi::TextureHandle PrefilteredEnvironment;
    nvrhi::BindingLayoutHandle Layout;
    nvrhi::BindingSetHandle Bindings;
  };
  std::vector<SceneBindingCache> m_SceneBindingCaches;
  ::Engine::Renderer::EnvironmentLightingResources m_EnvironmentLighting;
  bool m_EnvironmentLightingReady = false;
  nvrhi::ShaderHandle m_SkyboxVS;
  nvrhi::ShaderHandle m_SkyboxPS;
  nvrhi::BindingLayoutHandle m_SkyboxBindingLayout;
  nvrhi::BufferHandle m_SkyboxConstantsBuffer;
  nvrhi::SamplerHandle m_SkyboxSampler;
  std::map<std::vector<uint32_t>, nvrhi::GraphicsPipelineHandle> m_SkyboxPipelines;
  nvrhi::BindingLayoutHandle m_PerObjectBindingLayout;
  nvrhi::BufferHandle m_PerObjectBuffer;
  nvrhi::BindingSetHandle m_PerObjectBindingSet;
  size_t m_PerObjectBufferCapacity = 0;
  nvrhi::BindingLayoutHandle m_SceneBindingLayout;
  nvrhi::SamplerHandle m_ShadowSampler;
  nvrhi::ShaderHandle m_DebugVS;
  nvrhi::ShaderHandle m_DebugPS;
  nvrhi::ShaderHandle m_ShadowMaskPS;
  nvrhi::ShaderHandle m_AmbientOcclusionPrepassPS;
  nvrhi::BindingLayoutHandle m_DebugBindingLayout;
  nvrhi::BufferHandle m_DebugViewBuffer;
  nvrhi::BufferHandle m_DebugLineBuffer;
  nvrhi::BufferHandle m_DebugTriangleBuffer;
  uint64_t m_DebugLineBufferCapacity = 0;
  uint64_t m_DebugTriangleBufferCapacity = 0;

  ::Engine::Renderer::MaterialSystem* m_MaterialSystem = nullptr;
  std::function<std::shared_ptr<::Engine::Renderer::TextureResource>(const std::string&, bool, bool, bool, float)>
      m_AssetTextureResolver;
  const ::Engine::Renderer::ModelResourceLibrary* m_ModelResources = nullptr;
  using MaterialOverrideKey = std::tuple<uintptr_t, uint32_t, uint32_t, std::string, int32_t, uintptr_t>;
  struct MaterialOverrideCacheEntry {
    ::Engine::SceneMaterialDocument Source;
    std::shared_ptr<::Engine::Renderer::Material> Material;
  };
  std::map<MaterialOverrideKey, MaterialOverrideCacheEntry> m_MaterialOverrideCache;
  SubmissionStats m_SubmissionStats;
};

} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
