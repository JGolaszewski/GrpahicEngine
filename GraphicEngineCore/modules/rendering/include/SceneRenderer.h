#pragma once

#include "runtime/include/CoreExport.h"
#include <DirectXMath.h>
#include <cstddef>
#include <nvrhi/nvrhi.h>
#include <optional>
#include <unordered_map>
#include <vector>

#include "rendering/include/PipelineCache.h"
#include "scene/include/Scene.h"
#include "rendering/include/renderer/Camera.h"
#include "rendering/include/renderer/DebugGeometry.h"
#include "rendering/include/renderer/Entity.h"
#include "rendering/include/renderer/Light.h"
#include "rendering/include/renderer/MaterialSystem.h"
#include "rendering/include/renderer/RenderPacket.h"
#include "rendering/include/renderer/ShaderFactory.h"
#include "rendering/include/renderer/render_graph/FrameBlackboard.h"
#include "rendering/include/renderer/render_graph/FrameContext.h"
#include "rendering/include/renderer/render_graph/RenderView.h"

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine
{

class ENGINE_API SceneRenderer
{
  public:
    static constexpr uint32_t ShadowMapSize = 2048;
    static constexpr uint32_t MaxLights = ENGINE_MAX_LIGHTS;
    static constexpr uint32_t MaxShadowedLights = 4;
    static constexpr uint32_t PerObjectDataStride = 256;

    struct SubmissionStats
    {
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

    SceneRenderer(nvrhi::IDevice *device);
    void Init(nvrhi::IDevice *device, ::Engine::Renderer::MaterialSystem *materialSystem,
              ::Engine::Renderer::ShaderFactory *shaderFactory);

    void BuildViews(::Engine::Renderer::FrameBlackboard &blackboard, nvrhi::ICommandList *commandList,
                    ::Engine::Renderer::FrameContext &frameContext, ::Engine::Renderer::FrameDiagnostics &diagnostics);
    void BuildDebugGeometry(::Engine::Renderer::RenderView *view);
    void RenderDebugGeometry(nvrhi::ICommandList *commandList, const ::Engine::Renderer::RenderView &view);
    std::optional<Engine::LightId> PickLight(const ::Engine::Renderer::RenderView &view, float viewportX,
                                             float viewportY) const;
    void BuildLighting(Engine::Scene *scene, const std::vector<::Engine::Renderer::RenderView *> &shadowViews,
                       std::vector<::Engine::Renderer::DirectionalLightData> &lights);
    void BuildShadowData(Engine::Scene *scene, const std::vector<::Engine::Renderer::RenderView *> &shadowViews,
                         uint32_t firstShadowSlot, uint32_t maxShadowedLights);
    uint32_t CountShadowCasters(const Engine::Scene *scene, uint32_t limit) const;

    bool EnsureLightBuffers(size_t viewCount, ::Engine::Renderer::FrameContext &frameContext);
    nvrhi::BufferHandle GetLightBuffer(size_t viewIndex, ::Engine::Renderer::FrameContext &frameContext) const;
    nvrhi::BindingSetHandle GetSceneBindingSet(size_t viewIndex, nvrhi::BufferHandle lightBuffer,
                                               nvrhi::TextureHandle shadowMap,
                                               nvrhi::SamplerHandle shadowSampler,
                                               nvrhi::BufferHandle cameraBuffer,
                                               ::Engine::Renderer::FrameDiagnostics &diagnostics);
    inline nvrhi::BufferHandle GetPerObjectBuffer() const
    {
        return m_PerObjectBuffer;
    }
    inline nvrhi::BindingSetHandle GetPerObjectBindingSet() const
    {
        return m_PerObjectBindingSet;
    }
    inline nvrhi::SamplerHandle GetShadowSampler() const
    {
        return m_ShadowSampler;
    }
    inline nvrhi::BindingLayoutHandle GetPerObjectBindingLayout() const
    {
        return m_PerObjectBindingLayout;
    }
    inline nvrhi::BindingLayoutHandle GetSceneBindingLayout() const
    {
        return m_SceneBindingLayout;
    }
    inline ::Engine::Renderer::MaterialSystem *GetMaterialSystem() const
    {
        return m_MaterialSystem;
    }
    inline PipelineCache *GetPipelineCache()
    {
        return &m_PipelineCache;
    }
    inline const SubmissionStats &GetSubmissionStats() const
    {
        return m_SubmissionStats;
    }

  private:
    void CullAndFillView(::Engine::Renderer::RenderView *view);
    void SortViewQueues(::Engine::Renderer::RenderView *view);
    void EnsureDebugBuffer(nvrhi::BufferHandle &buffer, uint64_t &capacity, size_t vertexCount,
                           const char *debugName);
    nvrhi::GraphicsPipelineHandle GetDebugPipeline(const nvrhi::FramebufferInfo &framebuffer,
                                                   nvrhi::PrimitiveType primitiveType,
                                                   ::Engine::Renderer::DebugDepthMode depthMode);
    bool EnsurePerObjectResources(size_t maxDrawCount, ::Engine::Renderer::FrameContext &frameContext,
                                  ::Engine::Renderer::FrameDiagnostics &diagnostics);
    size_t EstimateDrawCount(const ::Engine::Renderer::FrameBlackboard &blackboard) const;
    ::Engine::Renderer::RenderPacket CreateRenderPacket(const ::Engine::Renderer::Mesh *mesh, const ::Engine::Renderer::Material *material);

    PipelineCache m_PipelineCache;

    nvrhi::DeviceHandle m_Device;
    struct SceneBindingCache
    {
        nvrhi::BufferHandle LightBuffer;
        nvrhi::TextureHandle ShadowMap;
        nvrhi::SamplerHandle ShadowSampler;
        nvrhi::BufferHandle CameraBuffer;
        nvrhi::BindingLayoutHandle Layout;
        nvrhi::BindingSetHandle Bindings;
    };
    std::vector<SceneBindingCache> m_SceneBindingCaches;
    nvrhi::BindingLayoutHandle m_PerObjectBindingLayout;
    nvrhi::BufferHandle m_PerObjectBuffer;
    nvrhi::BindingSetHandle m_PerObjectBindingSet;
    size_t m_PerObjectBufferCapacity = 0;
    nvrhi::BindingLayoutHandle m_SceneBindingLayout;
    nvrhi::SamplerHandle m_ShadowSampler;
    nvrhi::ShaderHandle m_DebugVS;
    nvrhi::ShaderHandle m_DebugPS;
    nvrhi::ShaderHandle m_ShadowMaskPS;
    nvrhi::BindingLayoutHandle m_DebugBindingLayout;
    nvrhi::BufferHandle m_DebugViewBuffer;
    nvrhi::BufferHandle m_DebugLineBuffer;
    nvrhi::BufferHandle m_DebugTriangleBuffer;
    uint64_t m_DebugLineBufferCapacity = 0;
    uint64_t m_DebugTriangleBufferCapacity = 0;

    ::Engine::Renderer::MaterialSystem *m_MaterialSystem = nullptr;
    SubmissionStats m_SubmissionStats;
};

} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
