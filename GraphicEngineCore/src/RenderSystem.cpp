#include "RenderSystem.h"
#include "AssetManager.h"
#include "renderer/render_graph/RenderPass.h"

#include <algorithm>
#include <iostream>
#include <utility>

namespace Renderer
{
class Camera;
}

namespace Engine
{
nvrhi::CommandListHandle CreateImmediateCommandList(IRenderBackend *backend)
{
    if (!backend || !backend->GetDevice())
    {
        return nullptr;
    }

    nvrhi::CommandListParameters parameters;
    parameters.setEnableImmediateExecution(true);
    return backend->GetDevice()->createCommandList(parameters);
}

RenderSystem::RenderSystem(IRenderBackend *backend)
    : m_RenderBackend(backend), m_CommandList(CreateImmediateCommandList(backend)),
      m_FrameContext(backend ? backend->GetDevice() : nullptr)
{
    if (!m_CommandList)
    {
        std::cerr << "ERROR: RenderSystem could not create its immediate command list." << std::endl;
        return;
    }

    m_ShaderFactory = std::make_unique<Renderer::ShaderFactory>(m_RenderBackend->GetDevice());
    auto m_DefaultVS = m_ShaderFactory->GetShader("shaders/color_vs.dxbc", nvrhi::ShaderType::Vertex);
    auto m_DefaultPS = m_ShaderFactory->GetShader("shaders/color_ps.dxbc", nvrhi::ShaderType::Pixel);
    auto m_TexturedPS =
        m_ShaderFactory->GetShader("shaders/color_textured_ps.dxbc", nvrhi::ShaderType::Pixel);

    m_ResourceManager = std::make_unique<Renderer::ResourceManager>(m_RenderBackend->GetDevice());
    m_MaterialSystem = std::make_unique<Renderer::MaterialSystem>(
        m_RenderBackend->GetDevice(), m_DefaultVS, m_DefaultPS, m_ResourceManager->GetFallbackTexture(true),
        m_ResourceManager->GetFallbackTexture(false));
    m_MaterialSystem->SetTexturedPixelShader(m_TexturedPS);
    m_SceneRenderer = std::make_unique<SceneRenderer>(m_RenderBackend->GetDevice());
    m_SceneRenderer->Init(m_RenderBackend->GetDevice(), m_MaterialSystem.get(), m_ShaderFactory.get());
    m_PrimitiveFactory =
        std::unique_ptr<Engine::PrimitiveFactory>(new Engine::PrimitiveFactory(m_RenderBackend->GetDevice()));

    m_RenderGraph = std::make_unique<Renderer::RenderGraph>(m_RenderBackend->GetDevice(), m_FrameContext);
}

std::shared_ptr<Engine::ViewportTarget> RenderSystem::CreateViewportTarget(uint32_t width, uint32_t height)
{
    if (!m_RenderBackend || !m_RenderBackend->GetDevice())
    {
        return nullptr;
    }

    auto target = std::shared_ptr<Engine::ViewportTarget>(
        new Engine::ViewportTarget(m_RenderBackend->GetDevice(), width, height));
    return target->IsValid() ? target : nullptr;
}

std::shared_ptr<Renderer::Entity> RenderSystem::CreatePrimitive(Engine::PrimitiveShape shape,
                                                                std::shared_ptr<Renderer::Material> material,
                                                                const DirectX::XMFLOAT3 &position,
                                                                const DirectX::XMFLOAT3 &scale)
{
    if (m_FrameActive || !m_PrimitiveFactory)
    {
        return nullptr;
    }

    return m_PrimitiveFactory->CreatePrimitive(shape, std::move(material), position, scale);
}

void RenderSystem::ExecuteImmediate(std::function<void(nvrhi::ICommandList *)> fn)
{
    if (m_FrameActive)
    {
        std::cerr << "ERROR: Immediate command-list execution is not allowed during an active frame."
                  << std::endl;
        return;
    }

    nvrhi::CommandListHandle cmd = CreateImmediateCommandList(m_RenderBackend);
    if (!cmd)
    {
        std::cerr << "ERROR: Could not create an immediate command list." << std::endl;
        return;
    }

    cmd->open();
    fn(cmd);
    cmd->close();
    m_RenderBackend->GetDevice()->executeCommandList(cmd);
}

void RenderSystem::BeginFrame()
{
    if (!m_CommandList)
    {
        return;
    }
    std::string uploadError;
    if (!m_ResourceManager->FlushUploads(uploadError))
    {
        std::cerr << "ERROR: Could not submit queued GPU uploads: " << uploadError << std::endl;
        return;
    }

    m_CommandList->open();
    m_FrameActive = true;
    m_RenderBackend->BeginGpuEvent(L"RenderFrame");
    ClearFrameBuffer();
    m_MaterialSystem->UpdateDirtyMaterials(m_CommandList);

    m_FrameContext.BeginFrame();

    m_RenderGraph->Reset();
}

void RenderSystem::SubmitViews(const std::vector<Engine::ViewportBinding> &views)
{
    if (!m_FrameActive)
        return;
    Renderer::FrameBlackboard &bb = m_RenderGraph->GetBlackboard();

    std::vector<Renderer::RenderView *> sceneViews;
    std::vector<Engine::Scene *> scenes;

    for (auto binding : views)
    {
        if (!binding.IsValid())
            continue;

        auto view = std::make_unique<Renderer::RenderView>();
        view->ViewType = Renderer::RenderView::Type::Main;
        view->Initialize(binding.SourceCamera, binding.TargetViewport);
        view->TargetScene = binding.TargetScene;
        view->DebugSettings = binding.DebugSettings;

        sceneViews.push_back(view.get());
        m_FrameContext.GetViews().push_back(std::move(view));

        if (std::find(scenes.begin(), scenes.end(), binding.TargetScene) == scenes.end())
            scenes.push_back(binding.TargetScene);
    }
    bb.SetSceneViews(std::move(sceneViews));

    std::vector<Renderer::RenderView *> shadowViews;
    shadowViews.reserve(scenes.size() * m_ShadowSettings.MaxShadowCastersPerScene);
    for (size_t sceneIndex = 0; sceneIndex < scenes.size(); ++sceneIndex)
    {
        const uint32_t firstShadowSlot = static_cast<uint32_t>(shadowViews.size());
        const uint32_t shadowCasterCount = m_SceneRenderer->CountShadowCasters(
            scenes[sceneIndex], m_ShadowSettings.MaxShadowCastersPerScene);
        for (uint32_t i = 0; i < shadowCasterCount; ++i)
        {
            auto view = std::make_unique<Renderer::RenderView>();
            view->ViewType = Renderer::RenderView::Type::Shadow;
            shadowViews.push_back(view.get());
            m_FrameContext.GetViews().push_back(std::move(view));
        }
        m_SceneRenderer->BuildShadowData(scenes[sceneIndex], shadowViews, firstShadowSlot, shadowCasterCount);
    }
    bb.SetShadowViews(std::move(shadowViews));
    bb.Set<uint32_t>("ShadowMapSize", m_ShadowSettings.Resolution);
    bb.Set<uint32_t>("MaxShadowedLights", (std::max)(1u, static_cast<uint32_t>(bb.GetShadowViews().size())));

    Renderer::FrameDiagnostics &diagnostics = m_FrameContext.GetDiagnostics();
    m_SceneRenderer->BuildViews(bb, m_CommandList, m_FrameContext, diagnostics);
    if (!m_SceneRenderer->EnsureLightBuffers(bb.GetSceneViews().size(), m_FrameContext))
    {
        std::cerr << "ERROR: Could not allocate per-view light buffers." << std::endl;
    }
    bb.SetPerObjectBuffer(m_SceneRenderer->GetPerObjectBuffer());
    bb.SetPerObjectBindingSet(m_SceneRenderer->GetPerObjectBindingSet());
    for (size_t viewIndex = 0; viewIndex < bb.GetSceneViews().size(); ++viewIndex)
    {
        Renderer::RenderView *view = bb.GetSceneViews()[viewIndex];
        if (view)
            view->LightBuffer = m_SceneRenderer->GetLightBuffer(viewIndex, m_FrameContext);
        if (view && view->TargetScene)
            m_SceneRenderer->BuildLighting(view->TargetScene, bb.GetShadowViews(), view->Lights);
    }
    bb.SetShadowSampler(m_SceneRenderer->GetShadowSampler());
    bb.SetPerObjectBindingLayout(m_SceneRenderer->GetPerObjectBindingLayout());
    bb.SetSceneBindingLayout(m_SceneRenderer->GetSceneBindingLayout());

    m_RenderGraph->AddPass(std::make_unique<Renderer::ShadowPass>());
    m_RenderGraph->AddPass(std::make_unique<Renderer::ForwardPass>(m_SceneRenderer.get()));
    m_RenderGraph->AddPass(std::make_unique<Renderer::DebugPass>(m_SceneRenderer.get()));
}

bool RenderSystem::SetShadowSettings(const ShadowSettings &settings) noexcept
{
    if (settings.Resolution < 128 || settings.Resolution > 8192 ||
        settings.MaxShadowCastersPerScene > SceneRenderer::MaxShadowedLights)
    {
        return false;
    }

    m_ShadowSettings = settings;
    return true;
}

std::shared_ptr<Renderer::Mesh> RenderSystem::GetOrCreateMesh(const std::string &guid, uint64_t generation,
                                                              const std::vector<Renderer::Vertex> &vertices,
                                                              const std::vector<uint32_t> &indices,
                                                              std::string &error)
{
    if (m_FrameActive || !m_ResourceManager)
    {
        error = "GPU mesh creation is only available outside an active frame";
        return nullptr;
    }
    return m_ResourceManager->GetOrCreateMesh(guid, generation, vertices, indices, error);
}

std::shared_ptr<Renderer::TextureResource> RenderSystem::GetOrCreateTexture(const std::string &identity,
                                                                            uint64_t generation,
                                                                            const ImportedImage &image,
                                                                            bool srgb, std::string &error,
                                                                            const ImportedTexture *sampler)
{
    if (m_FrameActive || !m_ResourceManager)
    {
        error = "texture upload requires an initialized renderer outside an active frame";
        return nullptr;
    }
    return m_ResourceManager->GetOrCreateTexture(identity, generation, image, srgb, error, sampler);
}

std::shared_ptr<Renderer::Material> RenderSystem::CreateMaterial(
    const Renderer::MaterialProperties &properties)
{
    return !m_FrameActive && m_MaterialSystem ? m_MaterialSystem->CreateMaterial(properties) : nullptr;
}

void RenderSystem::UnregisterMaterial(const std::shared_ptr<Renderer::Material> &material)
{
    if (!m_FrameActive && m_MaterialSystem)
        m_MaterialSystem->UnregisterMaterial(material);
}

bool RenderSystem::SetMaterialTextures(const std::shared_ptr<Renderer::Material> &material,
                                       std::shared_ptr<Renderer::TextureResource> baseColor,
                                       std::shared_ptr<Renderer::TextureResource> normal,
                                       std::optional<std::string> baseColorGuid,
                                       std::optional<std::string> normalGuid, bool setImportedDefaults)
{
    return !m_FrameActive && m_MaterialSystem &&
           m_MaterialSystem->SetTextureBindings(material, std::move(baseColor), std::move(normal),
                                                std::move(baseColorGuid), std::move(normalGuid),
                                                setImportedDefaults);
}

bool RenderSystem::SetPbrMaterialTextures(const std::shared_ptr<Renderer::Material> &material,
                                          std::shared_ptr<Renderer::TextureResource> baseColor,
                                          std::shared_ptr<Renderer::TextureResource> normal,
                                          std::shared_ptr<Renderer::TextureResource> metallicRoughness,
                                          std::shared_ptr<Renderer::TextureResource> occlusion,
                                          std::shared_ptr<Renderer::TextureResource> emissive,
                                          bool setImportedDefaults)
{
    return !m_FrameActive && m_MaterialSystem &&
           m_MaterialSystem->SetPbrTextureBindings(material, std::move(baseColor), std::move(normal),
                                                   std::move(metallicRoughness), std::move(occlusion),
                                                   std::move(emissive), setImportedDefaults);
}

void RenderSystem::ReleaseResource(const std::string &guid)
{
    if (m_ResourceManager)
        m_ResourceManager->Release(guid);
}

bool RenderSystem::UploadModel(const std::string &guid, uint64_t generation, const ImportedModel &model,
                               std::vector<std::vector<std::shared_ptr<Renderer::Mesh>>> &meshes,
                               std::string &error)
{
    if (m_FrameActive || !m_ResourceManager)
    {
        error = "GPU model uploads are only available outside an active frame";
        meshes.clear();
        return false;
    }
    return m_ResourceManager->GetOrCreateModelMeshes(guid, generation, model, meshes, error);
}

std::shared_ptr<Renderer::Entity> RenderSystem::CreateModelEntity(
    const std::string &guid, uint64_t generation, const ImportedModel &model,
    const std::vector<std::optional<Renderer::MaterialProperties>> &materialOverrides, std::string &error,
    std::vector<std::string> &warnings)
{
    using namespace DirectX;
    struct PendingSubmesh
    {
        size_t MeshIndex;
        size_t PrimitiveIndex;
        int32_t MaterialIndex;
        XMFLOAT4X4 LocalTransform;
        XMFLOAT3 BoundsMin;
        XMFLOAT3 BoundsMax;
    };
    std::vector<PendingSubmesh> pending;
    std::vector<uint8_t> visited(model.nodes.size(), 0);
    const auto visitNode = [&](const auto &self, uint32_t nodeIndex, FXMMATRIX parent) -> bool {
        if (nodeIndex >= model.nodes.size() || visited[nodeIndex] != 0)
            return false;
        visited[nodeIndex] = 1;
        const auto &node = model.nodes[nodeIndex];
        XMMATRIX local;
        if (node.hasMatrix)
        {
            local = XMLoadFloat4x4(&node.matrix);
        }
        else
        {
            local =
                XMMatrixAffineTransformation(XMLoadFloat3(&node.scale), XMVectorZero(),
                                             XMLoadFloat4(&node.rotation), XMLoadFloat3(&node.translation));
        }
        const XMMATRIX world = local * parent;
        if (node.meshIndex >= 0)
        {
            if (static_cast<size_t>(node.meshIndex) >= model.meshes.size())
                return false;
            const auto &sourceMesh = model.meshes[static_cast<size_t>(node.meshIndex)];
            for (size_t primitiveIndex = 0; primitiveIndex < sourceMesh.primitives.size(); ++primitiveIndex)
            {
                const auto &primitive = sourceMesh.primitives[primitiveIndex];
                XMFLOAT4X4 localTransform;
                XMStoreFloat4x4(&localTransform, world);
                pending.push_back({static_cast<size_t>(node.meshIndex), primitiveIndex,
                                   primitive.materialIndex, localTransform, primitive.boundsMin,
                                   primitive.boundsMax});
            }
        }
        for (const uint32_t child : node.children)
        {
            if (!self(self, child, world))
                return false;
        }
        return true;
    };

    std::vector<uint32_t> roots;
    if (model.defaultScene >= 0)
    {
        if (static_cast<size_t>(model.defaultScene) >= model.scenes.size())
        {
            error = "model default scene index is invalid";
            return nullptr;
        }
        roots = model.scenes[static_cast<size_t>(model.defaultScene)];
    }
    else if (!model.scenes.empty())
    {
        roots = model.scenes.front();
    }
    else
    {
        for (size_t i = 0; i < model.nodes.size(); ++i)
        {
            if (model.nodes[i].parentIndex < 0)
                roots.push_back(static_cast<uint32_t>(i));
        }
    }
    for (const uint32_t root : roots)
    {
        if (!visitNode(visitNode, root, XMMatrixIdentity()))
        {
            error = "model node hierarchy references invalid or repeated nodes";
            return nullptr;
        }
    }
    if (pending.empty())
    {
        error = "model's selected scene contains no renderable mesh primitives";
        return nullptr;
    }

    std::vector<std::vector<std::shared_ptr<Renderer::Mesh>>> meshes;
    if (!UploadModel(guid, generation, model, meshes, error))
        return nullptr;

    std::vector<std::shared_ptr<Renderer::Material>> materials;
    materials.reserve(model.materials.size() + 1);
    for (size_t materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex)
    {
        const auto &sourceMaterial = model.materials[materialIndex];
        Renderer::MaterialProperties properties;
        if (materialIndex < materialOverrides.size() && materialOverrides[materialIndex])
        {
            properties = *materialOverrides[materialIndex];
        }
        else
        {
            properties.BaseColor = sourceMaterial.baseColor;
            properties.Emissive = sourceMaterial.emissive;
            properties.Metallic = sourceMaterial.metallic;
            properties.Roughness = sourceMaterial.specularGlossiness ? 1.0f - sourceMaterial.glossiness
                                                                      : sourceMaterial.roughness;
            properties.Model = Renderer::BrdfModel::GGX;
            properties.Workflow = sourceMaterial.specularGlossiness
                                      ? Renderer::MaterialWorkflow::SpecularGlossiness
                                      : Renderer::MaterialWorkflow::MetallicRoughness;
            if (sourceMaterial.specularGlossiness)
                properties.SpecularF0 = sourceMaterial.specular;
        }
        if (materialIndex >= materialOverrides.size() || !materialOverrides[materialIndex])
        {
            properties.AlphaMode = sourceMaterial.alphaMode == "MASK" ? Renderer::MaterialAlphaMode::Mask
                                   : sourceMaterial.alphaMode == "BLEND"
                                       ? Renderer::MaterialAlphaMode::Blend
                                       : Renderer::MaterialAlphaMode::Opaque;
            properties.AlphaCutoff = sourceMaterial.alphaCutoff;
            properties.DoubleSided = sourceMaterial.doubleSided;
        }
        properties.BaseColorTexCoord = sourceMaterial.baseColorTexCoord;
        properties.MetallicRoughnessTexCoord = sourceMaterial.metallicRoughnessTexCoord;
        properties.NormalTexCoord = sourceMaterial.normalTexCoord;
        properties.OcclusionTexCoord = sourceMaterial.occlusionTexCoord;
        properties.EmissiveTexCoord = sourceMaterial.emissiveTexCoord;
        properties.NormalScale = sourceMaterial.normalScale;
        properties.OcclusionStrength = sourceMaterial.occlusionStrength;
        const auto setTransform = [](Renderer::MaterialTextureTransform &target,
                                     const ImportedTextureTransform &source) {
            target.Offset = source.offset;
            target.Scale = source.scale;
            target.Rotation = source.rotation;
        };
        setTransform(properties.TextureTransforms[0], sourceMaterial.baseColorTransform);
        setTransform(properties.TextureTransforms[1], sourceMaterial.normalTransform);
        setTransform(properties.TextureTransforms[2], sourceMaterial.metallicRoughnessTransform);
        setTransform(properties.TextureTransforms[3], sourceMaterial.occlusionTransform);
        setTransform(properties.TextureTransforms[4], sourceMaterial.emissiveTransform);
        auto material = m_MaterialSystem->CreateMaterial(properties);
        if (!material)
        {
            for (const auto &registered : materials)
                m_MaterialSystem->UnregisterMaterial(registered);
            error = "renderer could not create a material for the imported model";
            return nullptr;
        }
        auto baseColorTexture = m_ResourceManager->GetFallbackTexture(true);
        auto normalTexture = m_ResourceManager->GetFallbackTexture(false);
        auto metallicRoughnessTexture = m_ResourceManager->GetFallbackTexture(false);
        auto occlusionTexture = m_ResourceManager->GetFallbackTexture(false);
        auto emissiveTexture = m_ResourceManager->GetFallbackTexture(true);
        const auto resolveTexture = [&](int32_t textureIndex, bool srgb,
                                        std::shared_ptr<Renderer::TextureResource> &destination) -> bool {
            if (textureIndex < 0)
                return true;
            if (static_cast<size_t>(textureIndex) >= model.textures.size())
            {
                error = "imported material references an invalid texture slot";
                return false;
            }
            const auto &texture = model.textures[static_cast<size_t>(textureIndex)];
            const int32_t imageIndex = texture.imageIndex;
            if (imageIndex < 0 || static_cast<size_t>(imageIndex) >= model.images.size())
            {
                error = "imported texture references an invalid image slot";
                return false;
            }
            const auto &image = model.images[static_cast<size_t>(imageIndex)];
            destination = m_ResourceManager->GetOrCreateTexture(
                guid + "/texture/" + std::to_string(textureIndex), generation, image, srgb, error, &texture);
            return destination != nullptr;
        };
        if (!resolveTexture(sourceMaterial.baseColorTexture, true, baseColorTexture) ||
            !resolveTexture(sourceMaterial.normalTexture, false, normalTexture) ||
            !resolveTexture(sourceMaterial.metallicRoughnessTexture, sourceMaterial.specularGlossiness,
                            metallicRoughnessTexture) ||
            !resolveTexture(sourceMaterial.occlusionTexture, false, occlusionTexture) ||
            !resolveTexture(sourceMaterial.emissiveTexture, true, emissiveTexture) ||
            !m_MaterialSystem->SetPbrTextureBindings(material, baseColorTexture, normalTexture,
                                                     metallicRoughnessTexture, occlusionTexture,
                                                     emissiveTexture, true))
        {
            m_MaterialSystem->UnregisterMaterial(material);
            for (const auto &registered : materials)
                m_MaterialSystem->UnregisterMaterial(registered);
            if (error.empty())
                error = "renderer could not bind imported material textures";
            return nullptr;
        }
        materials.push_back(std::move(material));
    }
    Renderer::MaterialProperties fallbackProperties;
    fallbackProperties.Model = Renderer::BrdfModel::GGX;
    auto fallback = m_MaterialSystem->CreateMaterial(fallbackProperties);
    if (!fallback)
    {
        for (const auto &registered : materials)
            m_MaterialSystem->UnregisterMaterial(registered);
        error = "renderer could not create a fallback model material";
        return nullptr;
    }
    materials.push_back(std::move(fallback));

    auto entity = std::make_shared<Renderer::Entity>(nullptr, nullptr);
    entity->ModelGuid = guid;
    entity->Submeshes.reserve(pending.size());
    for (const auto &source : pending)
    {
        if (source.MeshIndex >= meshes.size() || source.PrimitiveIndex >= meshes[source.MeshIndex].size())
        {
            for (const auto &registered : materials)
                m_MaterialSystem->UnregisterMaterial(registered);
            error = "uploaded mesh cache does not match imported model primitives";
            return nullptr;
        }
        const size_t materialIndex =
            source.MaterialIndex < 0 ? materials.size() - 1 : static_cast<size_t>(source.MaterialIndex);
        if (materialIndex >= materials.size())
        {
            for (const auto &registered : materials)
                m_MaterialSystem->UnregisterMaterial(registered);
            error = "imported primitive references an invalid material slot";
            return nullptr;
        }
        Renderer::Submesh submesh;
        submesh.mesh = meshes[source.MeshIndex][source.PrimitiveIndex];
        submesh.material = materials[materialIndex];
        submesh.LocalTransform = source.LocalTransform;
        submesh.BoundsMin = source.BoundsMin;
        submesh.BoundsMax = source.BoundsMax;
        submesh.MaterialSlot = source.MaterialIndex;
        entity->Submeshes.push_back(std::move(submesh));
    }
    if (!entity->Submeshes.empty())
        entity->material = entity->Submeshes.front().material;
    return entity;
}

void RenderSystem::EndFrame()
{
    if (!m_CommandList || !m_FrameActive)
    {
        return;
    }

    if (!m_RenderGraph->Compile())
    {
        std::cerr << "ERROR: Render graph compilation failed: " << m_RenderGraph->GetLastError() << std::endl;
        ClearSubmittedTargets();
        m_CommandList->close();
        m_RenderBackend->GetDevice()->executeCommandList(m_CommandList);
        m_RenderBackend->EndGpuEvent();
        m_FrameActive = false;
        m_FrameContext.MarkSubmitted();
        m_ResourceManager->AdvanceSubmittedFrame(m_FrameContext.GetSubmittedFrames(),
                                                 m_FrameContext.GetCompletedFrames());
        return;
    }
    if (!m_RenderGraph->Execute(m_CommandList))
    {
        std::cerr << "ERROR: Render graph execution failed: " << m_RenderGraph->GetLastError() << std::endl;
        ClearSubmittedTargets();
    }

    m_CommandList->close();
    m_RenderBackend->GetDevice()->executeCommandList(m_CommandList);
    m_RenderBackend->EndGpuEvent();
    m_FrameActive = false;
    m_FrameContext.MarkSubmitted();
    m_ResourceManager->AdvanceSubmittedFrame(m_FrameContext.GetSubmittedFrames(),
                                             m_FrameContext.GetCompletedFrames());
}

std::optional<Engine::LightId> RenderSystem::PickLight(float viewportX, float viewportY) const
{
    if (!m_SceneRenderer)
    {
        return std::nullopt;
    }

    for (const auto &view : m_FrameContext.GetViews())
    {
        if (view && view->ViewType == Renderer::RenderView::Type::Main)
        {
            if (const auto lightId = m_SceneRenderer->PickLight(*view, viewportX, viewportY))
            {
                return lightId;
            }
        }
    }

    return std::nullopt;
}

void RenderSystem::ClearFrameBuffer()
{
    if (!m_CommandList)
    {
        return;
    }

    auto *frameBuffer = m_RenderBackend->GetDefaultFramebuffer();

    if (!frameBuffer)
    {
        return;
    }

    nvrhi::ITexture *colorTex = frameBuffer->getDesc().colorAttachments[0].texture;
    if (colorTex)
    {
        m_CommandList->clearTextureFloat(colorTex, nvrhi::AllSubresources,
                                         nvrhi::Color(0.1f, 0.1f, 0.1f, 1.0f));
    }

    nvrhi::ITexture *depthTex = frameBuffer->getDesc().depthAttachment.texture;
    if (depthTex)
    {
        m_CommandList->clearDepthStencilTexture(depthTex, nvrhi::AllSubresources, true, 1.0f, false, 0);
    }
}

void RenderSystem::ClearSubmittedTargets()
{
    if (!m_CommandList || !m_RenderGraph)
        return;

    for (Renderer::RenderView *view : m_RenderGraph->GetBlackboard().GetSceneViews())
    {
        if (!view || !view->TargetFramebuffer)
            continue;
        const nvrhi::FramebufferDesc &desc = view->TargetFramebuffer->getDesc();
        for (const nvrhi::FramebufferAttachment &attachment : desc.colorAttachments)
        {
            if (!attachment.texture)
                continue;
            m_CommandList->setTextureState(attachment.texture, attachment.subresources,
                                           nvrhi::ResourceStates::RenderTarget);
            m_CommandList->commitBarriers();
            m_CommandList->clearTextureFloat(attachment.texture, attachment.subresources,
                                             nvrhi::Color(0.15f, 0.0f, 0.15f, 1.0f));
        }
        if (desc.depthAttachment.valid() && desc.depthAttachment.texture)
        {
            m_CommandList->setTextureState(desc.depthAttachment.texture, desc.depthAttachment.subresources,
                                           nvrhi::ResourceStates::DepthWrite);
            m_CommandList->commitBarriers();
            m_CommandList->clearDepthStencilTexture(desc.depthAttachment.texture,
                                                    desc.depthAttachment.subresources, true, 1.0f, false, 0);
        }
    }
}
} // namespace Engine
