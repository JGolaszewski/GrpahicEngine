#pragma once
#include "AssetImporter.h"
#include "CoreExport.h"
#include "PrimitiveFactory.h"
#include "RenderBackend.h"
#include "SceneRenderer.h"
#include "ViewportTarget.h"
#include "renderer/MaterialSystem.h"
#include "renderer/RenderPacket.h"
#include "renderer/ResourceManager.h"
#include "renderer/ShaderFactory.h"
#include "renderer/Viewport.h"
#include "renderer/ViewportBinding.h"
#include "renderer/render_graph/FrameContext.h"
#include "renderer/render_graph/RenderGraph.h"
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace Engine
{
struct ShadowSettings
{
    uint32_t Resolution = SceneRenderer::ShadowMapSize;
    uint32_t MaxShadowCastersPerScene = SceneRenderer::MaxShadowedLights;
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
    std::shared_ptr<Renderer::Entity> CreatePrimitive(Engine::PrimitiveShape shape,
                                                      std::shared_ptr<Renderer::Material> material,
                                                      const DirectX::XMFLOAT3 &position,
                                                      const DirectX::XMFLOAT3 &scale = {1.0f, 1.0f, 1.0f});

    void ExecuteImmediate(std::function<void(nvrhi::ICommandList *)> fn);
    std::shared_ptr<Renderer::Mesh> GetOrCreateMesh(const std::string &guid, uint64_t generation,
                                                    const std::vector<Renderer::Vertex> &vertices,
                                                    const std::vector<uint32_t> &indices, std::string &error);
    std::shared_ptr<Renderer::TextureResource> GetOrCreateTexture(const std::string &identity,
                                                                  uint64_t generation,
                                                                  const ImportedImage &image, bool srgb,
                                                                  std::string &error);
    std::shared_ptr<Renderer::Material> CreateMaterial(const Renderer::MaterialProperties &properties);
    void UnregisterMaterial(const std::shared_ptr<Renderer::Material> &material);
    bool SetMaterialTextures(const std::shared_ptr<Renderer::Material> &material,
                             std::shared_ptr<Renderer::TextureResource> baseColor,
                             std::shared_ptr<Renderer::TextureResource> normal,
                             std::optional<std::string> baseColorGuid = {},
                             std::optional<std::string> normalGuid = {}, bool setImportedDefaults = false);
    void ReleaseResource(const std::string &guid);
    bool IsFrameActive() const noexcept
    {
        return m_FrameActive;
    }
    bool UploadModel(const std::string &guid, uint64_t generation, const ImportedModel &model,
                     std::vector<std::vector<std::shared_ptr<Renderer::Mesh>>> &meshes, std::string &error);
    std::shared_ptr<Renderer::Entity> CreateModelEntity(
        const std::string &guid, uint64_t generation, const ImportedModel &model,
        const std::vector<std::optional<Renderer::MaterialProperties>> &materialOverrides, std::string &error,
        std::vector<std::string> &warnings);

    SceneRenderer *GetSceneRenderer() const
    {
        return m_SceneRenderer.get();
    }
    Renderer::ShaderFactory *GetShaderFactory() const
    {
        return m_ShaderFactory.get();
    }
    Renderer::RenderGraph *GetRenderGraph() const
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
    ShadowSettings m_ShadowSettings;

    Renderer::FrameContext m_FrameContext;
    std::unique_ptr<Renderer::RenderGraph> m_RenderGraph;

    std::unique_ptr<SceneRenderer> m_SceneRenderer;
    std::unique_ptr<Renderer::MaterialSystem> m_MaterialSystem;
    std::unique_ptr<Renderer::ResourceManager> m_ResourceManager;
    std::unique_ptr<Renderer::ShaderFactory> m_ShaderFactory;
    std::unique_ptr<Engine::PrimitiveFactory> m_PrimitiveFactory;
};
} // namespace Engine
