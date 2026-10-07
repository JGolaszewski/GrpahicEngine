#include "Application.h"
#include "AssetManager.h"
#include "LogEngine.h"
#include "LogMacros.h"
#include "RenderBackend.h"
#include "RenderSystem.h"
#include "SceneDocument.h"
#include "Window.h"

#include "Scene.h"
#include "renderer/Camera.h"
#include "renderer/Entity.h"
#include "renderer/Light.h"
#include "renderer/Material.h"
#include "renderer/Viewport.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <thread>

namespace Engine
{
namespace
{
Renderer::BrdfModel ParseBrdfModel(const std::string &value)
{
    if (value == "ggx")
        return Renderer::BrdfModel::GGX;
    if (value == "gtr")
        return Renderer::BrdfModel::GTR;
    return Renderer::BrdfModel::Legacy;
}

Renderer::MaterialWorkflow ParseMaterialWorkflow(const std::string &value)
{
    return value == "specularGlossiness" ? Renderer::MaterialWorkflow::SpecularGlossiness
                                          : Renderer::MaterialWorkflow::MetallicRoughness;
}

const char *BrdfModelName(Renderer::BrdfModel value)
{
    switch (value)
    {
    case Renderer::BrdfModel::GGX:
        return "ggx";
    case Renderer::BrdfModel::GTR:
        return "gtr";
    default:
        return "legacy";
    }
}

Renderer::MaterialProperties ToMaterialProperties(const SceneMaterialDocument &document)
{
    Renderer::MaterialProperties properties;
    properties.BaseColor = {document.baseColor[0], document.baseColor[1], document.baseColor[2],
                            document.baseColor[3]};
    properties.Emissive = {document.emissive[0], document.emissive[1], document.emissive[2]};
    properties.Metallic = document.metallic;
    properties.Roughness = document.roughness;
    properties.GtrGamma = document.gtrGamma;
    properties.Ior = document.ior;
    properties.SpecularF0 = {document.specularF0[0], document.specularF0[1], document.specularF0[2]};
    properties.Model = ParseBrdfModel(document.brdfModel);
    properties.Workflow = ParseMaterialWorkflow(document.workflow);
    properties.SpecularMode = document.specularMode == "explicitF0"
                                  ? Renderer::DielectricSpecularMode::ExplicitF0
                                  : Renderer::DielectricSpecularMode::IndexOfRefraction;
    properties.AlphaMode = document.alphaMode == "mask"    ? Renderer::MaterialAlphaMode::Mask
                           : document.alphaMode == "blend" ? Renderer::MaterialAlphaMode::Blend
                                                           : Renderer::MaterialAlphaMode::Opaque;
    properties.AlphaCutoff = document.alphaCutoff;
    properties.DoubleSided = document.doubleSided;
    return properties;
}

SceneMaterialDocument ToMaterialDocument(const Renderer::MaterialProperties &properties)
{
    SceneMaterialDocument document;
    document.baseColor = {properties.BaseColor.x, properties.BaseColor.y, properties.BaseColor.z,
                          properties.BaseColor.w};
    document.emissive = {properties.Emissive.x, properties.Emissive.y, properties.Emissive.z};
    document.metallic = properties.Metallic;
    document.roughness = properties.Roughness;
    document.gtrGamma = properties.GtrGamma;
    document.ior = properties.Ior;
    document.specularF0 = {properties.SpecularF0.x, properties.SpecularF0.y, properties.SpecularF0.z};
    document.brdfModel = BrdfModelName(properties.Model);
    document.workflow = properties.Workflow == Renderer::MaterialWorkflow::SpecularGlossiness
                            ? "specularGlossiness"
                            : "metallicRoughness";
    document.specularMode =
        properties.SpecularMode == Renderer::DielectricSpecularMode::ExplicitF0 ? "explicitF0" : "ior";
    document.alphaMode = properties.AlphaMode == Renderer::MaterialAlphaMode::Mask    ? "mask"
                         : properties.AlphaMode == Renderer::MaterialAlphaMode::Blend ? "blend"
                                                                                      : "opaque";
    document.alphaCutoff = properties.AlphaCutoff;
    document.doubleSided = properties.DoubleSided;
    return document;
}

SceneMaterialDocument ToMaterialDocument(const Renderer::Material &material)
{
    auto document = ToMaterialDocument(material.GetProperties());
    document.baseColorTextureGuid = material.GetBaseColorTextureGuid();
    document.normalTextureGuid = material.GetNormalTextureGuid();
    return document;
}

std::optional<PrimitiveShape> ParsePrimitiveType(const std::string &type)
{
    if (type == "cube")
        return PrimitiveShape::Cube;
    if (type == "sphere")
        return PrimitiveShape::Sphere;
    if (type == "plane")
        return PrimitiveShape::Plane;
    if (type == "cone")
        return PrimitiveShape::Cone;
    return std::nullopt;
}
} // namespace

Application::Application() : m_AssetManager(std::make_unique<AssetManager>())
{
}
Application::~Application()
{
    m_ActiveViews.clear();
    m_ActiveScenes.clear();
    m_Viewports.clear();
    m_DefaultViewport.reset();
    if (m_RenderBackend && m_RenderBackend->GetDevice())
    {
        m_RenderBackend->GetDevice()->waitForIdle();
    }
    m_RenderSystem.reset();
    m_RenderBackend.reset();
    m_Window.reset();
    m_AssetManager.reset();
#if ENGINE_ENABLE_LOGGING
    Log::Flush();
    Log::Shutdown();
#endif
}

bool Application::InitializeCore()
{
#if ENGINE_ENABLE_LOGGING
    if (!Log::IsInitialized())
        Log::Initialize();
#endif
    ENGINE_LOG_INFO("Core", "Application initialization started | window_valid: {} | backend_valid: {}",
                    m_Window != nullptr, m_RenderBackend != nullptr);
    if (!m_Window || !m_RenderBackend)
    {
        ENGINE_LOG_ERROR("Core", "Application initialization rejected | window_valid: {} | backend_valid: {}",
                         m_Window != nullptr, m_RenderBackend != nullptr);
        return false;
    }
    if (!m_RenderBackend->Initialize(m_Window.get()))
    {
        ENGINE_LOG_ERROR("Render", "Renderer initialization failed | window_handle: {}",
                         reinterpret_cast<uintptr_t>(m_Window->GetNativeHandle()));
        return false;
    }

    m_RenderSystem = std::make_unique<RenderSystem>(m_RenderBackend.get());
    if (!m_RenderSystem->IsValid())
    {
        ENGINE_LOG_ERROR("Render", "Render system initialization failed | device_valid: {}",
                         m_RenderBackend->GetDevice() != nullptr);
        return false;
    }
    m_DefaultViewport = std::make_shared<Viewport>(m_Window->GetWidth(), m_Window->GetHeight(),
                                                   m_RenderBackend->GetDefaultFramebuffer());

    m_Window->SetEventCallback([this](Events::Event &event) { OnEvent(event); });

    ENGINE_LOG_INFO("Core", "Application initialized | framebuffer_width: {} | framebuffer_height: {}",
                    m_Window->GetWidth(), m_Window->GetHeight());

    return true;
}

void Application::Run(std::unique_ptr<IWindow> window, std::unique_ptr<IRenderBackend> backend)
{
    m_Window = std::move(window);
    m_RenderBackend = std::move(backend);
    if (!InitializeCore())
    {
        return;
    }

    m_IsRunning = true;
    OnInit();

    auto lastTime = std::chrono::high_resolution_clock::now();
    uint32_t lastWidth = m_Window->GetWidth();
    uint32_t lastHeight = m_Window->GetHeight();

    while (m_IsRunning && !m_Window->ShouldClose())
    {
#if ENGINE_ENABLE_LOGGING
        Log::SetFrameIndex(m_FrameTimingStats.FrameCount);
#endif
        const auto frameStart = std::chrono::steady_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        m_LastFrameDeltaTime = deltaTime;
        m_FrameTimingStats.UnclampedDeltaTime = deltaTime;

        m_Window->PollEvents();

        uint32_t currentWidth = m_Window->GetWidth();
        uint32_t currentHeight = m_Window->GetHeight();

        if (currentWidth > 0 && currentHeight > 0 &&
            (currentWidth != lastWidth || currentHeight != lastHeight))
        {
            m_RenderBackend->Resize(static_cast<int>(currentWidth), static_cast<int>(currentHeight));
            m_DefaultViewport = std::make_shared<Viewport>(currentWidth, currentHeight,
                                                           m_RenderBackend->GetDefaultFramebuffer());
            OnResize(currentWidth, currentHeight);
            lastWidth = currentWidth;
            lastHeight = currentHeight;
        }

        const float gameplayDeltaTime = (std::min)(deltaTime, 0.1f);
        m_FrameTimingStats.GameplayDeltaTime = gameplayDeltaTime;
        if (gameplayDeltaTime != deltaTime)
        {
            ++m_FrameTimingStats.ClampedFrameCount;
        }
        OnUpdate(gameplayDeltaTime);
        UpdateScenes(gameplayDeltaTime);

        ClearViews();

        OnSetupViews();
        RegisterDefaultView();

        m_RenderSystem->BeginFrame();

        m_RenderSystem->SubmitViews(m_ActiveViews);
        OnRender();

        m_RenderSystem->EndFrame();

        OnBeginUIFrame();
        OnUIRender();
        OnEndUIFrame();

        m_RenderBackend->Present();

        m_FrameTimingStats.CpuFrameTimeMs =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
        ++m_FrameTimingStats.FrameCount;
        if (m_FrameLimiterHz > 0)
        {
            const auto frameDuration =
                std::chrono::duration<float>(1.0f / static_cast<float>(m_FrameLimiterHz));
            std::this_thread::sleep_until(
                frameStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration));
        }
    }
    OnShutdownUI();
    ENGINE_LOG_INFO("Core", "Application run loop stopped | frame_count: {} | clamped_frame_count: {}",
                    m_FrameTimingStats.FrameCount, m_FrameTimingStats.ClampedFrameCount);
}

void Application::UpdateScenes(float deltaTime)
{
    for (const auto &scene : m_ActiveScenes)
    {
        if (scene)
        {
            scene->Update(deltaTime);
        }
    }
}

void Application::RegisterDefaultView()
{
    if (!m_ActiveViews.empty() || !m_DefaultViewport)
    {
        return;
    }

    for (const auto &scene : m_ActiveScenes)
    {
        if (!scene)
        {
            continue;
        }

        Renderer::Camera *camera = scene->GetCamera(0);
        if (!camera)
        {
            continue;
        }

        RegisterView({camera, m_DefaultViewport.get(), scene.get()});
        return;
    }
}

void Application::AddScene(std::shared_ptr<Scene> scene)
{
    m_ActiveScenes.push_back(scene);
    ENGINE_LOG_DEBUG("ECS", "Scene registered | scene_valid: {} | active_scene_count: {} | entity_count: {}",
                     scene != nullptr, m_ActiveScenes.size(), scene ? scene->GetEntities().size() : 0);
}

void Application::RemoveScene(std::shared_ptr<Scene> scene)
{
    auto it = std::find(m_ActiveScenes.begin(), m_ActiveScenes.end(), scene);
    if (it != m_ActiveScenes.end())
    {
        m_ActiveScenes.erase(it);
        ENGINE_LOG_DEBUG("ECS", "Scene unregistered | active_scene_count: {}", m_ActiveScenes.size());
    }
}

bool Application::LoadScene(const std::filesystem::path &path, std::string &error,
                            std::vector<std::string> &warnings)
{
    error.clear();
    warnings.clear();
    SceneDocumentResult loaded = m_AssetManager->LoadScene(path);
    if (!loaded)
    {
        error = std::move(loaded.error);
        warnings.clear();
        ENGINE_LOG_ERROR("Asset", "Scene load failed | path: {} | error: {}", path.generic_string(), error);
        return false;
    }
    warnings = std::move(loaded.warnings);
    const bool loadedSuccessfully = LoadSceneDocument(loaded.document, error, warnings);
    if (loadedSuccessfully)
        ENGINE_LOG_INFO("Asset", "Scene loaded | path: {} | entities: {} | lights: {} | cameras: {}",
                        path.generic_string(), loaded.document.entities.size(), loaded.document.lights.size(),
                        loaded.document.cameras.size());
    else
        ENGINE_LOG_ERROR("Asset", "Scene instantiation failed | path: {} | error: {}",
                         path.generic_string(), error);
    return loadedSuccessfully;
}

bool Application::LoadSceneDocument(const SceneDocument &document, std::string &error,
                                    std::vector<std::string> &warnings)
{
    error.clear();
    if (!m_RenderSystem || m_RenderSystem->IsFrameActive())
    {
        error = "scene loading requires an initialized renderer outside an active frame";
        return false;
    }

    auto stagedScene = std::make_shared<Scene>();
    stagedScene->SetUnitsPerMeter(document.unitsPerMeter);
    const auto fail = [&](std::string message) {
        if (m_RenderSystem)
        {
            for (const auto &entity : stagedScene->GetEntities())
            {
                if (!entity)
                    continue;
                if (entity->material)
                    m_RenderSystem->UnregisterMaterial(entity->material);
                for (const auto &submesh : entity->Submeshes)
                {
                    if (submesh.material)
                        m_RenderSystem->UnregisterMaterial(submesh.material);
                }
            }
        }
        error = std::move(message);
        ENGINE_LOG_ERROR("Core", "Scene document rejected | entities: {} | lights: {} | cameras: {} | error: {}",
                         document.entities.size(), document.lights.size(), document.cameras.size(), error);
        return false;
    };

    for (const auto &source : document.entities)
    {
        if (source.modelGuid && source.primitiveType)
        {
            return fail("entity '" + source.name + "' cannot specify both modelGuid and primitiveType");
        }
        if (source.materialGuid)
        {
            return fail("entity '" + source.name + "' references standalone material GUID " +
                        *source.materialGuid + "; standalone material assets are not supported yet");
        }

        std::shared_ptr<Renderer::Entity> entity;
        if (source.modelGuid)
        {
            if (!m_AssetManager->LoadModel(*source.modelGuid, error))
            {
                const std::string importError = std::move(error);
                return fail("entity '" + source.name + "' model " + *source.modelGuid + ": " + importError);
            }
            const auto model = m_AssetManager->GetModel(*source.modelGuid);
            const auto generation = m_AssetManager->GetGeneration(*source.modelGuid);
            if (!model || !generation)
                return fail("entity '" + source.name + "' model was not available after registry load");
            std::vector<std::optional<Renderer::MaterialProperties>> overrides;
            overrides.reserve(source.materialOverrides.size());
            for (const auto &material : source.materialOverrides)
            {
                overrides.push_back(material ? std::optional(ToMaterialProperties(*material)) : std::nullopt);
            }
            entity = m_RenderSystem->CreateModelEntity(*source.modelGuid, *generation, *model, overrides,
                                                       error, warnings);
            if (!entity)
            {
                const std::string uploadError = std::move(error);
                return fail("entity '" + source.name + "' model upload failed: " + uploadError);
            }
            std::set<int32_t> appliedSlots;
            for (const auto &submesh : entity->Submeshes)
            {
                if (submesh.MaterialSlot < 0 || !submesh.material ||
                    static_cast<size_t>(submesh.MaterialSlot) >= source.materialOverrides.size() ||
                    !source.materialOverrides[static_cast<size_t>(submesh.MaterialSlot)] ||
                    !appliedSlots.insert(submesh.MaterialSlot).second)
                    continue;
                const auto &materialDocument =
                    *source.materialOverrides[static_cast<size_t>(submesh.MaterialSlot)];
                auto baseColor = submesh.material->GetBaseColorTexture();
                auto normal = submesh.material->GetNormalTexture();
                const auto resolveOverride = [&](const std::optional<std::string> &textureGuid, bool srgb,
                                                 std::shared_ptr<Renderer::TextureResource> &destination) {
                    if (!textureGuid)
                        return;
                    std::string textureError;
                    if (!m_AssetManager->LoadTexture(*textureGuid, textureError))
                    {
                        warnings.push_back("entity '" + source.name + "' material slot " +
                                           std::to_string(submesh.MaterialSlot) + " texture " + *textureGuid +
                                           " is unavailable: " + textureError);
                        return;
                    }
                    const auto image = m_AssetManager->GetTexture(*textureGuid);
                    const auto generation = m_AssetManager->GetGeneration(*textureGuid);
                    if (!image || !generation)
                    {
                        warnings.push_back("texture " + *textureGuid +
                                           " has no decoded image data; using the material default");
                        return;
                    }
                    auto uploaded = m_RenderSystem->GetOrCreateTexture(*textureGuid, *generation, *image,
                                                                       srgb, textureError);
                    if (!uploaded)
                    {
                        warnings.push_back("texture " + *textureGuid +
                                           " could not be uploaded: " + textureError);
                        return;
                    }
                    destination = std::move(uploaded);
                };
                resolveOverride(materialDocument.baseColorTextureGuid, true, baseColor);
                resolveOverride(materialDocument.normalTextureGuid, false, normal);
                std::string bindingError;
                if (!m_RenderSystem->SetMaterialTextures(submesh.material, baseColor, normal,
                                                         materialDocument.baseColorTextureGuid,
                                                         materialDocument.normalTextureGuid))
                {
                    for (const auto &modelSubmesh : entity->Submeshes)
                    {
                        if (modelSubmesh.material)
                            m_RenderSystem->UnregisterMaterial(modelSubmesh.material);
                    }
                    return fail("entity '" + source.name + "' material texture binding failed");
                }
            }
        }
        else if (source.primitiveType)
        {
            const auto shape = ParsePrimitiveType(*source.primitiveType);
            if (!shape)
                return fail("entity '" + source.name + "' uses an unsupported primitive type");
            Renderer::MaterialProperties properties;
            if (source.material)
                properties = ToMaterialProperties(*source.material);
            auto material = m_RenderSystem->CreateMaterial(properties);
            if (!material)
                return fail("entity '" + source.name + "' material creation failed");
            entity = m_RenderSystem->CreatePrimitive(*shape, material, {0.0f, 0.0f, 0.0f});
            if (!entity)
            {
                m_RenderSystem->UnregisterMaterial(material);
                return fail("entity '" + source.name + "' primitive upload failed");
            }
            entity->PrimitiveType = source.primitiveType;
            if (source.material &&
                (source.material->baseColorTextureGuid || source.material->normalTextureGuid))
            {
                auto baseColor = entity->material->GetBaseColorTexture();
                auto normal = entity->material->GetNormalTexture();
                const auto resolveOverride = [&](const std::optional<std::string> &textureGuid, bool srgb,
                                                 std::shared_ptr<Renderer::TextureResource> &destination) {
                    if (!textureGuid)
                        return;
                    std::string textureError;
                    if (!m_AssetManager->LoadTexture(*textureGuid, textureError))
                    {
                        warnings.push_back("texture " + *textureGuid + " is unavailable: " + textureError);
                        return;
                    }
                    const auto image = m_AssetManager->GetTexture(*textureGuid);
                    const auto generation = m_AssetManager->GetGeneration(*textureGuid);
                    if (!image || !generation)
                        return;
                    auto uploaded = m_RenderSystem->GetOrCreateTexture(*textureGuid, *generation, *image,
                                                                       srgb, textureError);
                    if (uploaded)
                        destination = std::move(uploaded);
                    else
                        warnings.push_back("texture " + *textureGuid +
                                           " could not be uploaded: " + textureError);
                };
                resolveOverride(source.material->baseColorTextureGuid, true, baseColor);
                resolveOverride(source.material->normalTextureGuid, false, normal);
                if (!m_RenderSystem->SetMaterialTextures(entity->material, baseColor, normal,
                                                         source.material->baseColorTextureGuid,
                                                         source.material->normalTextureGuid))
                {
                    m_RenderSystem->UnregisterMaterial(entity->material);
                    return fail("entity '" + source.name + "' material texture binding failed");
                }
            }
        }
        else
        {
            return fail("entity '" + source.name + "' has neither modelGuid nor primitiveType");
        }

        entity->Name = source.name;
        entity->ModelGuid = source.modelGuid;
        entity->MaterialGuid = source.materialGuid;
        entity->transform.SetPosition(source.position[0], source.position[1], source.position[2]);
        entity->transform.Rotation = {source.rotation[0], source.rotation[1], source.rotation[2],
                                      source.rotation[3]};
        entity->transform.SetScale(source.scale[0], source.scale[1], source.scale[2]);
        entity->ForceWireframe = source.forceWireframe;
        entity->DisableDepthTest = source.disableDepthTest;
        entity->CastShadows = source.castShadows;
        entity->ReceiveShadows = source.receiveShadows;
        if (stagedScene->AddEntity(entity, source.id) == InvalidEntityId)
        {
            if (entity->material)
                m_RenderSystem->UnregisterMaterial(entity->material);
            for (const auto &submesh : entity->Submeshes)
            {
                if (submesh.material)
                    m_RenderSystem->UnregisterMaterial(submesh.material);
            }
            return fail("scene contains a duplicate or unsupported entity ID " + std::to_string(source.id));
        }
    }

    for (const auto &source : document.lights)
    {
        auto light = std::make_shared<Renderer::SpotLight>(
            DirectX::XMFLOAT3(source.direction[0], source.direction[1], source.direction[2]),
            DirectX::XMFLOAT3(source.color[0], source.color[1], source.color[2]), source.intensity);
        light->SetUsePosition(source.usePosition);
        light->SetPosition({source.position[0], source.position[1], source.position[2]});
        light->SetRange(source.range);
        light->SetConeAngles(source.innerAngleDegrees, source.outerAngleDegrees);
        light->SetCastShadows(source.castShadows);
        light->SetShadowBias(source.shadowBias);
        light->SetShadowNormalBias(source.shadowNormalBias);
        if (stagedScene->AddLight(std::move(light), source.id) == InvalidLightId)
        {
            return fail("scene contains a duplicate or unsupported light ID " + std::to_string(source.id));
        }
    }

    for (const auto &source : document.cameras)
    {
        auto camera =
            std::make_shared<Renderer::Camera>(source.fovRadians, source.nearPlane, source.farPlane);
        camera->m_Transform.SetPosition(source.position[0], source.position[1], source.position[2]);
        camera->m_Transform.Rotation = {source.rotation[0], source.rotation[1], source.rotation[2],
                                        source.rotation[3]};
        if (stagedScene->AddCamera(std::move(camera), source.id) == InvalidCameraId)
        {
            return fail("scene contains a duplicate or unsupported camera ID " + std::to_string(source.id));
        }
    }

    m_ActiveViews.clear();
    m_ActiveScenes.clear();
    m_ActiveScenes.push_back(stagedScene);
    OnSceneReplaced(stagedScene);
    ENGINE_LOG_INFO("Core", "Active scene replaced | entities: {} | lights: {} | cameras: {}",
                    document.entities.size(), document.lights.size(), document.cameras.size());
    return true;
}

bool Application::SaveScene(const std::filesystem::path &path, const Scene &scene, std::string &error) const
{
    return m_AssetManager->SaveScene(path, CreateSceneDocument(scene), error);
}

SceneDocument Application::CreateSceneDocument(const Scene &scene) const
{
    SceneDocument document;
    document.unitsPerMeter = scene.GetUnitsPerMeter();
    for (const auto &entity : scene.GetEntities())
    {
        if (!entity)
            continue;
        SceneEntityDocument item;
        item.id = scene.GetEntityId(entity.get());
        item.name = entity->Name;
        item.modelGuid = entity->ModelGuid;
        item.materialGuid = entity->MaterialGuid;
        item.primitiveType = entity->PrimitiveType;
        item.position = {entity->transform.Position.x, entity->transform.Position.y,
                         entity->transform.Position.z};
        item.rotation = {entity->transform.Rotation.x, entity->transform.Rotation.y,
                         entity->transform.Rotation.z, entity->transform.Rotation.w};
        item.scale = {entity->transform.Scale.x, entity->transform.Scale.y, entity->transform.Scale.z};
        item.forceWireframe = entity->ForceWireframe;
        item.disableDepthTest = entity->DisableDepthTest;
        item.castShadows = entity->CastShadows;
        item.receiveShadows = entity->ReceiveShadows;
        if (entity->material && !entity->ModelGuid)
            item.material = ToMaterialDocument(*entity->material);
        if (entity->ModelGuid)
        {
            size_t materialSlotCount = 0;
            for (const auto &submesh : entity->Submeshes)
            {
                if (submesh.MaterialSlot >= 0)
                    materialSlotCount =
                        (std::max)(materialSlotCount, static_cast<size_t>(submesh.MaterialSlot) + 1);
            }
            item.materialOverrides.resize(materialSlotCount);
            for (const auto &submesh : entity->Submeshes)
            {
                if (submesh.MaterialSlot >= 0 && submesh.material)
                {
                    item.materialOverrides[static_cast<size_t>(submesh.MaterialSlot)] =
                        ToMaterialDocument(*submesh.material);
                }
            }
        }
        document.entities.push_back(std::move(item));
    }

    for (size_t i = 0; i < scene.GetLights().size(); ++i)
    {
        const auto &light = scene.GetLights()[i];
        if (!light)
            continue;
        SceneLightDocument item;
        item.id = scene.GetLightIdAt(i);
        const auto direction = light->GetDirection();
        const auto position = light->GetPosition();
        const auto color = light->GetColor();
        item.direction = {direction.x, direction.y, direction.z};
        item.position = {position.x, position.y, position.z};
        item.color = {color.x, color.y, color.z};
        item.intensity = light->GetIntensity();
        item.range = light->GetRange();
        item.innerAngleDegrees = light->GetInnerAngleDegrees();
        item.outerAngleDegrees = light->GetOuterAngleDegrees();
        item.shadowBias = light->GetShadowBias();
        item.shadowNormalBias = light->GetShadowNormalBias();
        item.usePosition = light->GetUsePosition();
        item.castShadows = light->GetCastShadows();
        document.lights.push_back(std::move(item));
    }

    for (size_t i = 0; i < scene.GetCameras().size(); ++i)
    {
        const auto &camera = scene.GetCameras()[i];
        if (!camera)
            continue;
        SceneCameraDocument item;
        item.id = scene.GetCameraIdAt(i);
        const auto &transform = camera->m_Transform;
        item.position = {transform.Position.x, transform.Position.y, transform.Position.z};
        item.rotation = {transform.Rotation.x, transform.Rotation.y, transform.Rotation.z,
                         transform.Rotation.w};
        item.fovRadians = camera->GetFovRadians();
        item.nearPlane = camera->GetNearPlane();
        item.farPlane = camera->GetFarPlane();
        document.cameras.push_back(std::move(item));
    }
    return document;
}

void Application::AddViewport(std::shared_ptr<Engine::Viewport> viewport)
{
    m_Viewports.push_back(viewport);
}

void Application::RemoveViewport(std::shared_ptr<Engine::Viewport> viewport)
{
    auto it = std::find(m_Viewports.begin(), m_Viewports.end(), viewport);
    if (it != m_Viewports.end())
    {
        m_Viewports.erase(it);
    }
}

void Application::RegisterView(const ViewportBinding &binding)
{
    if (binding.IsValid())
    {
        m_ActiveViews.push_back(binding);
    }
}

IWindow *Application::GetWindow() const
{
    return m_Window.get();
}

SceneRenderer *Application::GetSceneRenderer() const
{
    return m_RenderSystem ? m_RenderSystem->GetSceneRenderer() : nullptr;
}

Renderer::ShaderFactory *Application::GetShaderFactory() const
{
    return m_RenderSystem ? m_RenderSystem->GetShaderFactory() : nullptr;
}

void Application::ExecuteImmediate(std::function<void(nvrhi::ICommandList *)> fn)
{
    if (m_RenderSystem)
    {
        m_RenderSystem->ExecuteImmediate(std::move(fn));
    }
}
} // namespace Engine
