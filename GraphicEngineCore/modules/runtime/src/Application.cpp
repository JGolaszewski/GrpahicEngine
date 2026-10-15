#include "runtime/include/Application.h"
#include "assets/include/AssetManager.h"
#include "runtime/include/LogEngine.h"
#include "runtime/include/LogMacros.h"
#include "rendering/include/RenderBackend.h"
#include "rendering/include/RenderSystem.h"
#include "scene/include/SceneDocument.h"
#include "runtime/include/Window.h"

#include "scene/include/Scene.h"
#include "rendering/include/renderer/Camera.h"
#include "rendering/include/renderer/Entity.h"
#include "rendering/include/renderer/Light.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Viewport.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <thread>

namespace Engine
{
namespace
{
::Engine::Renderer::BrdfModel ParseBrdfModel(const std::string &value)
{
    if (value == "ggx")
        return ::Engine::Renderer::BrdfModel::GGX;
    if (value == "gtr")
        return ::Engine::Renderer::BrdfModel::GTR;
    return ::Engine::Renderer::BrdfModel::Legacy;
}

::Engine::Renderer::MaterialWorkflow ParseMaterialWorkflow(const std::string &value)
{
    return value == "specularGlossiness" ? ::Engine::Renderer::MaterialWorkflow::SpecularGlossiness
                                         : ::Engine::Renderer::MaterialWorkflow::MetallicRoughness;
}

const char *BrdfModelName(::Engine::Renderer::BrdfModel value)
{
    switch (value)
    {
    case ::Engine::Renderer::BrdfModel::GGX:
        return "ggx";
    case ::Engine::Renderer::BrdfModel::GTR:
        return "gtr";
    default:
        return "legacy";
    }
}

::Engine::Renderer::MaterialProperties ToMaterialProperties(const SceneMaterialDocument &document)
{
    ::Engine::Renderer::MaterialProperties properties;
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
                                  ? ::Engine::Renderer::DielectricSpecularMode::ExplicitF0
                                  : ::Engine::Renderer::DielectricSpecularMode::IndexOfRefraction;
    properties.AlphaMode = document.alphaMode == "mask"    ? ::Engine::Renderer::MaterialAlphaMode::Mask
                           : document.alphaMode == "blend" ? ::Engine::Renderer::MaterialAlphaMode::Blend
                                                           : ::Engine::Renderer::MaterialAlphaMode::Opaque;
    properties.AlphaCutoff = document.alphaCutoff;
    properties.DoubleSided = document.doubleSided;
    return properties;
}

SceneMaterialDocument ToMaterialDocument(const ::Engine::Renderer::MaterialProperties &properties)
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
    document.workflow = properties.Workflow == ::Engine::Renderer::MaterialWorkflow::SpecularGlossiness
                            ? "specularGlossiness"
                            : "metallicRoughness";
    document.specularMode = properties.SpecularMode == ::Engine::Renderer::DielectricSpecularMode::ExplicitF0
                                ? "explicitF0"
                                : "ior";
    document.alphaMode = properties.AlphaMode == ::Engine::Renderer::MaterialAlphaMode::Mask    ? "mask"
                         : properties.AlphaMode == ::Engine::Renderer::MaterialAlphaMode::Blend ? "blend"
                                                                                                : "opaque";
    document.alphaCutoff = properties.AlphaCutoff;
    document.doubleSided = properties.DoubleSided;
    return document;
}

SceneMaterialDocument ToMaterialDocument(const ::Engine::Renderer::Material &material)
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

Application::Application()
    : m_FrameMetricsHistory(std::make_unique<std::array<FrameMetricsSample, FrameMetricsHistoryCapacity>>()),
      m_AssetManager(std::make_unique<AssetManager>())
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
    if (Log::IsInitialized())
    {
        const Log::HealthSnapshot health = Log::GetHealthSnapshot();
        if (!health.breadcrumbPath.empty())
            Log::DumpCrashBreadcrumbs(health.breadcrumbPath);
    }
    Log::Flush();
    Log::Shutdown();
#endif
}

bool Application::InitializeCore()
{
#if ENGINE_ENABLE_LOGGING
    const bool loggerWasInitialized = Log::IsInitialized();
    const bool loggerReady = loggerWasInitialized
                                 ? Log::GetHealthSnapshot().fileStatus != Log::FileSinkStatus::Failed
                                 : Log::Initialize();
    if (!loggerReady && !loggerWasInitialized)
    {
        const Log::HealthSnapshot health = Log::GetHealthSnapshot();
        ENGINE_LOG_ERROR_CODE("Diagnostics", "logging.initialize.sink_failed",
                              "Logger initialized with an unavailable file sink | path: {} | error: {}",
                              health.jsonlPath.generic_string(), health.lastSinkError);
    }
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
        FrameTimingStats frameTimingStats = m_FrameTimingStats;
        frameTimingStats.ObjectsSubmitted = 0;
        frameTimingStats.ObjectsCulled = 0;
        frameTimingStats.PacketsBuilt = 0;
        frameTimingStats.OpaquePackets = 0;
        frameTimingStats.MaskedPackets = 0;
        frameTimingStats.TransparentPackets = 0;
        frameTimingStats.PipelineCacheHits = 0;
        frameTimingStats.PipelineCacheMisses = 0;
        frameTimingStats.PipelineCreations = 0;
        frameTimingStats.ViewsSubmitted = 0;
        frameTimingStats.DrawCalls = 0;
        frameTimingStats.BufferAllocations = 0;
        frameTimingStats.UploadBytes = 0;
        frameTimingStats.CullingTimeMs = 0.0f;
        frameTimingStats.PacketBuildTimeMs = 0.0f;

        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        m_LastFrameDeltaTime = deltaTime;
        frameTimingStats.UnclampedDeltaTime = deltaTime;

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
        frameTimingStats.GameplayDeltaTime = gameplayDeltaTime;
        if (gameplayDeltaTime != deltaTime)
        {
            ++frameTimingStats.ClampedFrameCount;
        }
        auto phaseStart = std::chrono::steady_clock::now();
        OnUpdate(gameplayDeltaTime);
        if (!IsSimulationPaused())
            UpdateScenes(gameplayDeltaTime);
        frameTimingStats.UpdateTimeMs =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - phaseStart).count();

        ClearViews();

        phaseStart = std::chrono::steady_clock::now();
        OnSetupViews();
        RegisterDefaultView();
        frameTimingStats.ViewSetupTimeMs =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - phaseStart).count();

        phaseStart = std::chrono::steady_clock::now();
        m_RenderSystem->BeginFrame();
        const bool renderFrameStarted = m_RenderSystem->IsFrameActive();

        m_RenderSystem->SubmitViews(m_ActiveViews);
        OnRender();

        m_RenderSystem->EndFrame();
        frameTimingStats.RenderTimeMs =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - phaseStart).count();

        phaseStart = std::chrono::steady_clock::now();
        OnBeginUIFrame();
        OnUIRender();
        OnEndUIFrame();
        frameTimingStats.UiTimeMs =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - phaseStart).count();

        phaseStart = std::chrono::steady_clock::now();
        m_RenderBackend->Present();
        frameTimingStats.PresentTimeMs =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - phaseStart).count();

        frameTimingStats.CpuFrameTimeMs =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
        if (renderFrameStarted)
        {
            if (const SceneRenderer *sceneRenderer = m_RenderSystem->GetSceneRenderer())
            {
                const SceneRenderer::SubmissionStats &submission = sceneRenderer->GetSubmissionStats();
                frameTimingStats.ObjectsSubmitted = submission.ObjectsSubmitted;
                frameTimingStats.ObjectsCulled = submission.ObjectsCulled;
                frameTimingStats.PacketsBuilt = submission.PacketsBuilt;
                frameTimingStats.OpaquePackets = submission.OpaquePackets;
                frameTimingStats.MaskedPackets = submission.MaskedPackets;
                frameTimingStats.TransparentPackets = submission.TransparentPackets;
                frameTimingStats.PipelineCacheHits = submission.PipelineCacheHits;
                frameTimingStats.PipelineCacheMisses = submission.PipelineCacheMisses;
                frameTimingStats.PipelineCreations = submission.PipelineCreations;
                frameTimingStats.CullingTimeMs = static_cast<float>(submission.CullingTimeMs);
                frameTimingStats.PacketBuildTimeMs = static_cast<float>(submission.PacketBuildTimeMs);
            }
            const ::Engine::Renderer::FrameDiagnostics &renderDiagnostics =
                m_RenderSystem->GetFrameDiagnostics();
            frameTimingStats.ViewsSubmitted = m_RenderSystem->GetSubmittedViewCount();
            frameTimingStats.DrawCalls = renderDiagnostics.DrawCalls;
            frameTimingStats.BufferAllocations = renderDiagnostics.BufferAllocations;
            frameTimingStats.UploadBytes = renderDiagnostics.UploadBytes;
        }
        const ::Engine::Renderer::GpuFrameTiming gpuTiming = m_RenderSystem->GetGpuFrameTiming();
        frameTimingStats.GpuFrameTimeMs = gpuTiming.TimeMs;
        frameTimingStats.GpuTimingAvailable = gpuTiming.Available;
        frameTimingStats.GpuTimingPending = gpuTiming.Pending;
        frameTimingStats.GpuTimingFrameIndex = gpuTiming.FrameIndex;
        ++frameTimingStats.FrameCount;
        if (m_FrameLimiterHz > 0)
        {
            const auto frameDuration =
                std::chrono::duration<float>(1.0f / static_cast<float>(m_FrameLimiterHz));
            const auto limiterStart = std::chrono::steady_clock::now();
            std::this_thread::sleep_until(
                frameStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration));
            frameTimingStats.LimiterWaitMs =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - limiterStart)
                    .count();
        }
        else
        {
            frameTimingStats.LimiterWaitMs = 0.0f;
        }
        frameTimingStats.FrameTimeMs = frameTimingStats.CpuFrameTimeMs + frameTimingStats.LimiterWaitMs;
        m_FrameTimingStats = frameTimingStats;
        (*m_FrameMetricsHistory)[(frameTimingStats.FrameCount - 1) % FrameMetricsHistoryCapacity] = {
            frameTimingStats.FrameCount - 1,
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count()),
            frameTimingStats};
        m_FrameMetricsCount = (std::min)(m_FrameMetricsCount + 1, FrameMetricsHistoryCapacity);
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

        ::Engine::Renderer::Camera *camera = scene->GetCamera(0);
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
    Log::ScopedOperation operation;
    error.clear();
    warnings.clear();
    SceneDocumentResult loaded = m_AssetManager->LoadScene(path);
    if (!loaded)
    {
        error = std::move(loaded.error);
        warnings.clear();
        ENGINE_LOG_ERROR_CODE("Asset", "asset.scene.load_failed", "Scene load failed | path: {} | error: {}",
                              path.generic_string(), error);
        return false;
    }
    warnings = std::move(loaded.warnings);
    const bool loadedSuccessfully = LoadSceneDocument(loaded.document, error, warnings);
    if (loadedSuccessfully)
        ENGINE_LOG_INFO("Asset", "Scene loaded | path: {} | entities: {} | lights: {} | cameras: {}",
                        path.generic_string(), loaded.document.entities.size(), loaded.document.lights.size(),
                        loaded.document.cameras.size());
    else
        ENGINE_LOG_ERROR_CODE("Asset", "asset.scene.instantiate_failed",
                              "Scene instantiation failed | path: {} | error: {}", path.generic_string(),
                              error);
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
        ENGINE_LOG_ERROR("Core",
                         "Scene document rejected | entities: {} | lights: {} | cameras: {} | error: {}",
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

        std::shared_ptr<::Engine::Renderer::Entity> entity;
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
            std::vector<std::optional<::Engine::Renderer::MaterialProperties>> overrides;
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
                const auto resolveOverride =
                    [&](const std::optional<std::string> &textureGuid, bool srgb,
                        std::shared_ptr<::Engine::Renderer::TextureResource> &destination) {
                        if (!textureGuid)
                            return;
                        std::string textureError;
                        if (!m_AssetManager->LoadTexture(*textureGuid, textureError))
                        {
                            warnings.push_back("entity '" + source.name + "' material slot " +
                                               std::to_string(submesh.MaterialSlot) + " texture " +
                                               *textureGuid + " is unavailable: " + textureError);
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
            ::Engine::Renderer::MaterialProperties properties;
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
                const auto resolveOverride =
                    [&](const std::optional<std::string> &textureGuid, bool srgb,
                        std::shared_ptr<::Engine::Renderer::TextureResource> &destination) {
                        if (!textureGuid)
                            return;
                        std::string textureError;
                        if (!m_AssetManager->LoadTexture(*textureGuid, textureError))
                        {
                            warnings.push_back("texture " + *textureGuid +
                                               " is unavailable: " + textureError);
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

    for (const auto &source : document.entities)
    {
        if (!source.parentId)
            continue;
        const auto child = stagedScene->FindEntity(source.id);
        const auto parent = stagedScene->FindEntity(*source.parentId);
        if (!child || !parent || !child->SetParent(parent.get()))
            warnings.push_back("entity " + std::to_string(source.id) +
                               " could not be attached to its saved parent and remains at the scene root");
    }

    for (const auto &source : document.lights)
    {
        auto light = std::make_shared<::Engine::Renderer::SpotLight>(
            DirectX::XMFLOAT3(source.direction[0], source.direction[1], source.direction[2]),
            DirectX::XMFLOAT3(source.color[0], source.color[1], source.color[2]), source.intensity);
        light->Name = source.name;
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
            std::make_shared<::Engine::Renderer::Camera>(source.fovRadians, source.nearPlane, source.farPlane);
        camera->Name = source.name;
        camera->transform.SetPosition(source.position[0], source.position[1], source.position[2]);
        camera->transform.Rotation = {source.rotation[0], source.rotation[1], source.rotation[2],
                                        source.rotation[3]};
        if (stagedScene->AddCamera(std::move(camera), source.id) == InvalidCameraId)
        {
            return fail("scene contains a duplicate or unsupported camera ID " + std::to_string(source.id));
        }
    }

    struct PendingParent
    {
        Engine::Object *Child = nullptr;
        Engine::SceneNodeParentDocument Parent;
        std::string Description;
    };
    std::vector<PendingParent> pendingParents;
    pendingParents.reserve(document.entities.size() + document.lights.size() + document.cameras.size());
    for (const auto &source : document.entities)
    {
        if (source.parent)
            pendingParents.push_back({stagedScene->FindEntity(source.id).get(), *source.parent,
                                      "entity " + std::to_string(source.id)});
    }
    for (const auto &source : document.lights)
    {
        if (source.parent)
            pendingParents.push_back({stagedScene->FindLight(source.id).get(), *source.parent,
                                      "light " + std::to_string(source.id)});
    }
    for (const auto &source : document.cameras)
    {
        if (source.parent)
            pendingParents.push_back({stagedScene->FindCamera(source.id).get(), *source.parent,
                                      "camera " + std::to_string(source.id)});
    }
    std::stable_sort(pendingParents.begin(), pendingParents.end(), [](const auto &left, const auto &right) {
        if (left.Parent.type != right.Parent.type)
            return left.Parent.type < right.Parent.type;
        if (left.Parent.id != right.Parent.id)
            return left.Parent.id < right.Parent.id;
        return left.Parent.siblingOrder < right.Parent.siblingOrder;
    });
    const auto resolveParent = [&stagedScene](const Engine::SceneNodeParentDocument &parent) -> Engine::Object * {
        if (parent.type == "entity")
            return stagedScene->FindEntity(parent.id).get();
        if (parent.type == "light")
            return stagedScene->FindLight(parent.id).get();
        if (parent.type == "camera")
            return stagedScene->FindCamera(parent.id).get();
        return nullptr;
    };
    for (const auto &pending : pendingParents)
    {
        Engine::Object *parent = resolveParent(pending.Parent);
        if (!pending.Child || !parent)
        {
            warnings.push_back(pending.Description + " references a missing parent; placed at scene root");
            continue;
        }
        if (!pending.Child->SetParent(parent, Engine::Object::ParentTransformMode::KeepLocal))
            warnings.push_back(pending.Description + " has an invalid or cyclic parent; placed at scene root");
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
    Log::ScopedOperation operation;
    if (m_AssetManager->SaveScene(path, CreateSceneDocument(scene), error))
        return true;
    ENGINE_LOG_ERROR_CODE("Asset", "asset.scene.save_failed", "Scene save failed | path: {} | error: {}",
                          path.generic_string(), error);
    return false;
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
        if (const auto *parentEntity = dynamic_cast<const ::Engine::Renderer::Entity *>(entity->GetParent()))
            item.parentId = scene.GetEntityId(parentEntity);
        item.name = entity->Name;
        if (entity->GetParent() && entity->GetParent() != &scene.GetRoot())
        {
            const auto children = entity->GetParent()->GetChildren();
            const auto sibling = std::find_if(children.begin(), children.end(), [&](const auto &candidate) {
                return candidate.get() == entity.get();
            });
            const uint32_t siblingOrder = sibling == children.end() ? 0u : static_cast<uint32_t>(sibling - children.begin());
            if (const auto *parentEntity = dynamic_cast<const ::Engine::Renderer::Entity *>(entity->GetParent()))
                item.parent = Engine::SceneNodeParentDocument{"entity", scene.GetEntityId(parentEntity), siblingOrder};
            else if (const auto *parentLight = dynamic_cast<const ::Engine::Renderer::SpotLight *>(entity->GetParent()))
                item.parent = Engine::SceneNodeParentDocument{"light", scene.GetLightId(parentLight), siblingOrder};
            else if (const auto *parentCamera = dynamic_cast<const ::Engine::Renderer::Camera *>(entity->GetParent()))
                item.parent = Engine::SceneNodeParentDocument{"camera", scene.GetCameraId(parentCamera), siblingOrder};
        }
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
        item.name = light->Name;
        if (light->GetParent() && light->GetParent() != &scene.GetRoot())
        {
            const auto children = light->GetParent()->GetChildren();
            const auto sibling = std::find_if(children.begin(), children.end(), [&](const auto &candidate) {
                return candidate.get() == light.get();
            });
            const uint32_t siblingOrder = sibling == children.end() ? 0u : static_cast<uint32_t>(sibling - children.begin());
            if (const auto *parentEntity = dynamic_cast<const ::Engine::Renderer::Entity *>(light->GetParent()))
                item.parent = Engine::SceneNodeParentDocument{"entity", scene.GetEntityId(parentEntity), siblingOrder};
            else if (const auto *parentLight = dynamic_cast<const ::Engine::Renderer::SpotLight *>(light->GetParent()))
                item.parent = Engine::SceneNodeParentDocument{"light", scene.GetLightId(parentLight), siblingOrder};
            else if (const auto *parentCamera = dynamic_cast<const ::Engine::Renderer::Camera *>(light->GetParent()))
                item.parent = Engine::SceneNodeParentDocument{"camera", scene.GetCameraId(parentCamera), siblingOrder};
        }
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
        item.name = camera->Name;
        if (camera->GetParent() && camera->GetParent() != &scene.GetRoot())
        {
            const auto children = camera->GetParent()->GetChildren();
            const auto sibling = std::find_if(children.begin(), children.end(), [&](const auto &candidate) {
                return candidate.get() == camera.get();
            });
            const uint32_t siblingOrder = sibling == children.end() ? 0u : static_cast<uint32_t>(sibling - children.begin());
            if (const auto *parentEntity = dynamic_cast<const ::Engine::Renderer::Entity *>(camera->GetParent()))
                item.parent = Engine::SceneNodeParentDocument{"entity", scene.GetEntityId(parentEntity), siblingOrder};
            else if (const auto *parentLight = dynamic_cast<const ::Engine::Renderer::SpotLight *>(camera->GetParent()))
                item.parent = Engine::SceneNodeParentDocument{"light", scene.GetLightId(parentLight), siblingOrder};
            else if (const auto *parentCamera = dynamic_cast<const ::Engine::Renderer::Camera *>(camera->GetParent()))
                item.parent = Engine::SceneNodeParentDocument{"camera", scene.GetCameraId(parentCamera), siblingOrder};
        }
        const auto &transform = camera->transform;
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

::Engine::Renderer::ShaderFactory *Application::GetShaderFactory() const
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
