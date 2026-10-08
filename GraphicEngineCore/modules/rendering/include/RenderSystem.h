#pragma once
#include "assets/include/AssetImporter.h"
#include "runtime/include/CoreExport.h"
#include "rendering/include/PrimitiveFactory.h"
#include "rendering/include/RenderBackend.h"
#include "rendering/include/SceneRenderer.h"
#include "rendering/include/ViewportTarget.h"
#include "rendering/include/renderer/MaterialSystem.h"
#include "rendering/include/renderer/RenderPacket.h"
#include "rendering/include/renderer/ResourceManager.h"
#include "rendering/include/renderer/ShaderFactory.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/include/renderer/ViewportBinding.h"
#include "rendering/include/renderer/render_graph/FrameContext.h"
#include "rendering/include/renderer/render_graph/RenderGraph.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine
{
struct ShadowSettings
{
    uint32_t Resolution = Scene::Engine::Renderer::ShadowMapSize;
    uint32_t MaxShadowCastersPerScene = Scene::Engine::Renderer::MaxShadowedLights;
};

class ENGINE_API RenderSystem
{
  public:
    RenderSystem(IRenderBackend *backend);
    bool IsValid() const
    {
        return m_CommandList != nullptr;
    }

    void BeginFrame();
    void SubmitViews(const std::vector<Engine::ViewportBinding> &views);
    void EndFrame();
    bool SetShadowSettings(const ShadowSettings &settings) noexcept;
    const ShadowSettings &GetShadowSettings() const noexcept
    {
        return m_ShadowSettings;
    }
    std::optional<Engine::LightId> PickLight(float viewportX, float viewportY) const;
    std::shared_ptr<Engine::ViewportTarget> CreateViewportTarget(uint32_t width, uint32_t height);
    std::shared_ptr<::Engine::Renderer::Entity> CreatePrimitive(Engine::PrimitiveShape shape,
                                                      std::shared_ptr<::Engine::Renderer::Material> material,
                                                      const DirectX::XMFLOAT3 &position,
                                                      const DirectX::XMFLOAT3 &scale = {1.0f, 1.0f, 1.0f});

    void ExecuteImmediate(std::function<void(nvrhi::ICommandList *)> fn);
    std::shared_ptr<::Engine::Renderer::Mesh> GetOrCreateMesh(const std::string &guid, uint64_t generation,
                                                    const std::vector<::Engine::Renderer::Vertex> &vertices,
                                                    const std::vector<uint32_t> &indices, std::string &error);
    std::shared_ptr<::Engine::Renderer::TextureResource> GetOrCreateTexture(const std::string &identity,
                                                                  uint64_t generation,
                                                                  const ImportedImage &image, bool srgb,
                                                                  std::string &error,
                                                                  const ImportedTexture *sampler = nullptr);
    std::shared_ptr<::Engine::Renderer::Material> CreateMaterial(const ::Engine::Renderer::MaterialProperties &properties);
    void UnregisterMaterial(const std::shared_ptr<::Engine::Renderer::Material> &material);
    bool SetMaterialTextures(const std::shared_ptr<::Engine::Renderer::Material> &material,
                             std::shared_ptr<::Engine::Renderer::TextureResource> baseColor,
                             std::shared_ptr<::Engine::Renderer::TextureResource> normal,
                             std::optional<std::string> baseColorGuid = {},
                             std::optional<std::string> normalGuid = {}, bool setImportedDefaults = false);
    bool SetPbrMaterialTextures(const std::shared_ptr<::Engine::Renderer::Material> &material,
                                std::shared_ptr<::Engine::Renderer::TextureResource> baseColor,
                                std::shared_ptr<::Engine::Renderer::TextureResource> normal,
                                std::shared_ptr<::Engine::Renderer::TextureResource> metallicRoughness,
                                std::shared_ptr<::Engine::Renderer::TextureResource> occlusion,
                                std::shared_ptr<::Engine::Renderer::TextureResource> emissive,
                                bool setImportedDefaults = false);
    void ReleaseResource(const std::string &guid);
    bool IsFrameActive() const noexcept
    {
        return m_FrameActive;
    }
    bool UploadModel(const std::string &guid, uint64_t generation, const ImportedModel &model,
                     std::vector<std::vector<std::shared_ptr<::Engine::Renderer::Mesh>>> &meshes, std::string &error);
    std::shared_ptr<::Engine::Renderer::Entity> CreateModelEntity(
        const std::string &guid, uint64_t generation, const ImportedModel &model,
        const std::vector<std::optional<::Engine::Renderer::MaterialProperties>> &materialOverrides, std::string &error,
        std::vector<std::string> &warnings);

    SceneRenderer *GetSceneRenderer() const
    {
        return m_SceneRenderer.get();
    }
    const ::Engine::Renderer::FrameDiagnostics &GetFrameDiagnostics() const noexcept
    {
        return m_FrameContext.GetDiagnostics();
    }
    ::Engine::Renderer::GpuFrameTiming GetGpuFrameTiming() const noexcept
    {
        return m_FrameContext.GetGpuFrameTiming();
    }
    uint32_t GetSubmittedViewCount() const noexcept
    {
        return static_cast<uint32_t>(m_FrameContext.GetBlackboard().GetSceneViews().size() +
                                     m_FrameContext.GetBlackboard().GetShadowViews().size());
    }
    ::Engine::Renderer::ShaderFactory *GetShaderFactory() const
    {
        return m_ShaderFactory.get();
    }
    ::Engine::Renderer::RenderGraph *GetRenderGraph() const
    {
        return m_RenderGraph.get();
    }
    Engine::PrimitiveFactory *GetPrimitiveFactory() const
    {
        return m_PrimitiveFactory.get();
    }
    IRenderBackend *GetRenderBackend() const
    {
        return m_RenderBackend;
    }

  private:
    void ClearFrameBuffer();
    void ClearSubmittedTargets();

    IRenderBackend *const m_RenderBackend;
    nvrhi::CommandListHandle m_CommandList;
    bool m_FrameActive = false;
    std::string m_LastUploadFailure;
    std::string m_LastRenderGraphFailure;
    ShadowSettings m_ShadowSettings;

    ::Engine::Renderer::FrameContext m_FrameContext;
    std::unique_ptr<::Engine::Renderer::RenderGraph> m_RenderGraph;

    std::unique_ptr<SceneRenderer> m_SceneRenderer;
    std::unique_ptr<::Engine::Renderer::MaterialSystem> m_MaterialSystem;
    std::unique_ptr<::Engine::Renderer::ResourceManager> m_ResourceManager;
    std::unique_ptr<::Engine::Renderer::ShaderFactory> m_ShaderFactory;
    std::unique_ptr<Engine::PrimitiveFactory> m_PrimitiveFactory;
};
} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
