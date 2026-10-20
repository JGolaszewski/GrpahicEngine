#include "runtime/include/Application.h"
#include "assets/include/AssetManager.h"
#include "rendering/include/RenderBackend.h"
#include "rendering/include/RenderSystem.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/src/SceneRenderer.h"
#include "rendering/src/render_graph/FrameContext.h"
#include "runtime/include/LogEngine.h"
#include "runtime/include/LogMacros.h"
#include "runtime/include/Window.h"
#include "scene/include/SceneDocument.h"

#include "rendering/include/renderer/Viewport.h"
#include "scene/include/Scene.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <nvrhi/nvrhi.h>
#include <optional>
#include <string_view>
#include <thread>

namespace Engine {
namespace {
auto IsPrimitiveType(const std::string& type) -> bool {
  return type == "cube" || type == "sphere" || type == "plane" || type == "cone";
}
} // namespace

Application::Application()
    : m_FrameMetricsHistory(std::make_unique<std::array<FrameMetricsSample, FrameMetricsHistoryCapacity>>()),
      m_AssetManager(std::make_unique<AssetManager>()) {}
Application::~Application() {
  m_ActiveViews.clear();
  m_ActiveScenes.clear();
  m_Viewports.clear();
  m_DefaultViewport.reset();
  if (m_RenderBackend && (m_RenderBackend->GetDevice() != nullptr)) {
    static_cast<nvrhi::IDevice*>(m_RenderBackend->GetDevice())->waitForIdle();
  }
  m_RenderSystem.reset();
  m_RenderBackend.reset();
  m_Window.reset();
  m_AssetManager.reset();
#if ENGINE_ENABLE_LOGGING
  if (Log::IsInitialized()) {
    const Log::HealthSnapshot health = Log::GetHealthSnapshot();
    if (!health.breadcrumbPath.empty()) {
      Log::DumpCrashBreadcrumbs(health.breadcrumbPath);
    }
  }
  Log::Flush();
  Log::Shutdown();
#endif
}

auto Application::InitializeCore() -> bool {
#if ENGINE_ENABLE_LOGGING
  const bool loggerWasInitialized = Log::IsInitialized();
  const bool loggerReady =
      loggerWasInitialized ? Log::GetHealthSnapshot().fileStatus != Log::FileSinkStatus::Failed : Log::Initialize();
  if (!loggerReady && !loggerWasInitialized) {
    const Log::HealthSnapshot health = Log::GetHealthSnapshot();
    ENGINE_LOG_ERROR_CODE("Diagnostics", "logging.initialize.sink_failed",
                          "Logger initialized with an unavailable file sink | path: {} | error: {}",
                          health.jsonlPath.generic_string(), health.lastSinkError);
  }
#endif
  ENGINE_LOG_INFO("Core", "Application initialization started | window_valid: {} | backend_valid: {}",
                  m_Window != nullptr, m_RenderBackend != nullptr);
  if (!m_Window || !m_RenderBackend) {
    ENGINE_LOG_ERROR("Core", "Application initialization rejected | window_valid: {} | backend_valid: {}",
                     m_Window != nullptr, m_RenderBackend != nullptr);
    return false;
  }
  if (!m_RenderBackend->Initialize(m_Window.get())) {
    ENGINE_LOG_ERROR(
        "Render", "Renderer initialization failed | window_handle: {}",
        reinterpret_cast<uintptr_t>(m_Window->GetNativeHandle()) // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                                                                 // // Report the Win32 HWND value as an integer.
    );
    return false;
  }

  m_RenderSystem = std::make_unique<RenderSystem>(m_RenderBackend.get());
  if (!m_RenderSystem->IsValid()) {
    ENGINE_LOG_ERROR("Render", "Render system initialization failed | device_valid: {}",
                     m_RenderBackend->GetDevice() != nullptr);
    return false;
  }
  m_RenderSystem->SetAssetManager(m_AssetManager.get());
  m_DefaultViewport =
      std::make_shared<Viewport>(m_Window->GetWidth(), m_Window->GetHeight(), m_RenderBackend->GetDefaultFramebuffer());

  m_Window->SetEventCallback([this](Events::Event& event) -> void { OnEvent(event); });

  ENGINE_LOG_INFO("Core", "Application initialized | framebuffer_width: {} | framebuffer_height: {}",
                  m_Window->GetWidth(), m_Window->GetHeight());

  return true;
}

void Application::Run(std::unique_ptr<IWindow> window, std::unique_ptr<IRenderBackend> backend) {
  m_Window = std::move(window);
  m_RenderBackend = std::move(backend);
  if (!InitializeCore()) {
    return;
  }

  m_IsRunning = true;
  OnInit();

  auto lastTime = std::chrono::high_resolution_clock::now();
  uint32_t lastWidth = m_Window->GetWidth();
  uint32_t lastHeight = m_Window->GetHeight();

  while (m_IsRunning && !m_Window->ShouldClose()) {
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
    frameTimingStats.CullingTimeMs = 0.0F;
    frameTimingStats.PacketBuildTimeMs = 0.0F;

    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    m_LastFrameDeltaTime = deltaTime;
    frameTimingStats.UnclampedDeltaTime = deltaTime;

    m_Window->PollEvents();

    uint32_t currentWidth = m_Window->GetWidth();
    uint32_t currentHeight = m_Window->GetHeight();

    if (currentWidth > 0 && currentHeight > 0 && (currentWidth != lastWidth || currentHeight != lastHeight)) {
      m_RenderBackend->Resize(static_cast<int>(currentWidth), static_cast<int>(currentHeight));
      m_DefaultViewport =
          std::make_shared<Viewport>(currentWidth, currentHeight, m_RenderBackend->GetDefaultFramebuffer());
      OnResize(currentWidth, currentHeight);
      lastWidth = currentWidth;
      lastHeight = currentHeight;
    }

    const float gameplayDeltaTime = (std::min)(deltaTime, 0.1F);
    frameTimingStats.GameplayDeltaTime = gameplayDeltaTime;
    if (gameplayDeltaTime != deltaTime) {
      ++frameTimingStats.ClampedFrameCount;
    }
    auto phaseStart = std::chrono::steady_clock::now();
    OnUpdate(gameplayDeltaTime);
    if (!IsSimulationPaused()) {
      UpdateScenes(gameplayDeltaTime);
    }
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
    if (renderFrameStarted) {
      if (const SceneRenderer* sceneRenderer = m_RenderSystem->GetSceneRenderer()) {
        const SceneRenderer::SubmissionStats& submission = sceneRenderer->GetSubmissionStats();
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
      const ::Engine::Renderer::FrameDiagnostics& renderDiagnostics = m_RenderSystem->GetFrameDiagnostics();
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
    if (m_FrameLimiterHz > 0) {
      const auto frameDuration = std::chrono::duration<float>(1.0F / static_cast<float>(m_FrameLimiterHz));
      const auto limiterStart = std::chrono::steady_clock::now();
      std::this_thread::sleep_until(frameStart +
                                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration));
      frameTimingStats.LimiterWaitMs =
          std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - limiterStart).count();
    } else {
      frameTimingStats.LimiterWaitMs = 0.0F;
    }
    frameTimingStats.FrameTimeMs = frameTimingStats.CpuFrameTimeMs + frameTimingStats.LimiterWaitMs;
    m_FrameTimingStats = frameTimingStats;
    (*m_FrameMetricsHistory).at((frameTimingStats.FrameCount - 1) % FrameMetricsHistoryCapacity) = {
        .FrameIndex = frameTimingStats.FrameCount - 1,
        .TimestampNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count()),
        .Timing = frameTimingStats,
    };
    m_FrameMetricsCount = (std::min)(m_FrameMetricsCount + 1, FrameMetricsHistoryCapacity);
  }
  OnShutdownUI();
  ENGINE_LOG_INFO("Core", "Application run loop stopped | frame_count: {} | clamped_frame_count: {}",
                  m_FrameTimingStats.FrameCount, m_FrameTimingStats.ClampedFrameCount);
}

void Application::UpdateScenes(float deltaTime) {
  for (const auto& scene : m_ActiveScenes) {
    if (scene) {
      const Status updated = scene->Update(deltaTime);
      if (!updated) {
        ENGINE_LOG_ERROR("ECS", "Scene update failed | reason: {}", updated.error().Message);
      }
    }
  }
}

void Application::RegisterDefaultView() {
  if (!m_ActiveViews.empty() || !m_DefaultViewport) {
    return;
  }

  for (const auto& scene : m_ActiveScenes) {
    if (!scene) {
      continue;
    }

    std::optional<EntityHandle> cameraEntity;
    std::optional<Renderer::CameraView> cameraView;
    scene->World().ForEachChunkReadOnly<Ecs::Camera, Ecs::WorldTransform, Ecs::GlobalVisibility>(
        [&cameraEntity, &cameraView](std::span<const EntityHandle> entities, std::span<const Ecs::Camera> cameras,
                                     std::span<const Ecs::WorldTransform> transforms,
                                     std::span<const Ecs::GlobalVisibility> visibility) -> void {
          if (cameraView) {
            return;
          }
          for (size_t row = 0; row < entities.size(); ++row) {
            if (cameras[row].Active && visibility[row].IsVisible) {
              cameraView = Renderer::ResolveSceneCameraView(cameras[row], transforms[row]);
              if (!cameraView) {
                continue;
              }
              cameraEntity = entities[row];
              return;
            }
          }
        });
    if (!cameraEntity || !cameraView) {
      continue;
    }

    RegisterView({.Camera = *cameraView,
                  .CameraEntity = *cameraEntity,
                  .TargetViewport = m_DefaultViewport.get(),
                  .TargetScene = scene.get()});
    return;
  }
}

void Application::AddScene(const std::shared_ptr<Scene>& scene) {
  m_ActiveScenes.push_back(scene);
  ENGINE_LOG_DEBUG("ECS", "Scene registered | scene_valid: {} | active_scene_count: {} | entity_count: {}",
                   scene != nullptr, m_ActiveScenes.size(), scene ? scene->EntityCount() : 0);
}

void Application::RemoveScene(const std::shared_ptr<Scene>& scene) {
  auto it = std::ranges::find(m_ActiveScenes, scene);
  if (it != m_ActiveScenes.end()) {
    m_ActiveScenes.erase(it);
    ENGINE_LOG_DEBUG("ECS", "Scene unregistered | active_scene_count: {}", m_ActiveScenes.size());
  }
}

auto Application::LoadScene(const std::filesystem::path& path, std::string& error, std::vector<std::string>& warnings)
    -> bool {
  Log::ScopedOperation operation;
  error.clear();
  warnings.clear();
  SceneDocumentResult loaded = m_AssetManager->LoadScene(path);
  if (!loaded) {
    error = std::move(loaded.error);
    warnings.clear();
    ENGINE_LOG_ERROR_CODE("Asset", "asset.scene.load_failed", "Scene load failed | path: {} | error: {}",
                          path.generic_string(), error);
    return false;
  }
  warnings = std::move(loaded.warnings);
  const bool loadedSuccessfully = LoadSceneDocument(loaded.document, error, warnings);
  if (loadedSuccessfully) {
    const auto& scenes = GetScenes();
    if (!scenes.empty()) {
      std::string sceneName = path.filename().string();
      constexpr std::string_view sceneExtension = ".scene.json";
      if (sceneName.ends_with(sceneExtension)) {
        sceneName.resize(sceneName.size() - sceneExtension.size());
      } else {
        sceneName = path.stem().string();
      }
      (void)scenes.back()->QueueSet(scenes.back()->SceneRootEntity(), Ecs::Name{.Value = sceneName});
      for (const auto& result : scenes.back()->sync()) {
        if (!result.Outcome) {
          error = result.Outcome.error().Message;
          return false;
        }
      }
    }
    ENGINE_LOG_INFO("Asset", "Scene loaded | path: {} | entities: {} | lights: {} | cameras: {}", path.generic_string(),
                    loaded.document.entities.size(), loaded.document.lights.size(), loaded.document.cameras.size());
  } else {
    ENGINE_LOG_ERROR_CODE("Asset", "asset.scene.instantiate_failed",
                          "Scene instantiation failed | path: {} | error: {}", path.generic_string(), error);
  }
  return loadedSuccessfully;
}

// This transaction stages and validates every scene object type before committing the replacement.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto Application::LoadSceneDocument(const SceneDocument& document, std::string& error,
                                    std::vector<std::string>& warnings) -> bool {
  error.clear();
  warnings.clear();
  for (const SceneLightDocument& source : document.lights) {
    if (source.type != "directional" && source.type != "point" && source.type != "spot") {
      error = "light '" + source.name + "': unsupported light type '" + source.type + "'";
      return false;
    }
    const Ecs::LightType type = source.type == "point"                        ? Ecs::LightType::Point
                                : source.type == "spot" || source.usePosition ? Ecs::LightType::Spot
                                                                              : Ecs::LightType::Directional;
    const Ecs::Light candidate{.Type = type,
                               .Direction = source.direction,
                               .Color = source.color,
                               .Intensity = source.intensity,
                               .Range = source.range,
                               .InnerAngleDegrees = source.innerAngleDegrees,
                               .OuterAngleDegrees = source.outerAngleDegrees,
                               .ShadowBias = source.shadowBias,
                               .ShadowNormalBias = source.shadowNormalBias,
                               .CastShadows = source.castShadows};
    if (const Status lightValidation = Ecs::Validate(candidate); !lightValidation) {
      error = "light '" + source.name + "': " + lightValidation.error().Message;
      return false;
    }
  }
  auto stagedScene = std::make_shared<Scene>();
  stagedScene->SetUnitsPerMeter(document.unitsPerMeter);

  struct PendingEntity {
    EntityHandle Handle;
    const SceneEntityDocument* Document = nullptr;
  };
  struct PendingLight {
    EntityHandle Handle;
    const SceneLightDocument* Document = nullptr;
  };
  struct PendingCamera {
    EntityHandle Handle;
    const SceneCameraDocument* Document = nullptr;
  };
  std::vector<const SceneEntityDocument*> entitiesToCreate;
  std::vector<const SceneLightDocument*> lightsToCreate;
  std::vector<const SceneCameraDocument*> camerasToCreate;
  const SceneEntityDocument* rootDocument = nullptr;

  const auto prepareMaterialTextures = [this, &warnings](const SceneMaterialDocument& material,
                                                         const std::string& entityName) -> void {
    if (m_RenderSystem == nullptr) {
      return;
    }
    const auto prepare = [this, &warnings, &entityName](const std::optional<std::string>& guid, bool srgb,
                                                        bool normalMap, bool preserveAlphaCoverage, float alphaCutoff,
                                                        std::string_view slotName) -> void {
      if (!guid) {
        return;
      }
      std::string textureError;
      if (!m_RenderSystem->PrepareAssetTexture(*guid, srgb, textureError, normalMap, preserveAlphaCoverage,
                                               alphaCutoff)) {
        warnings.push_back("entity '" + entityName + "': could not prepare " + std::string(slotName) + " texture " +
                           *guid + ": " + textureError);
      }
    };
    prepare(material.baseColorTextureGuid, true, false, material.alphaMode == "mask", material.alphaCutoff,
            "base-color");
    prepare(material.normalTextureGuid, false, true, false, 0.5F, "normal");
  };

  for (const auto& entity : document.entities) {
    if (entity.sceneRoot) {
      if (rootDocument != nullptr) {
        error = "scene document contains multiple scene roots";
        return false;
      }
      rootDocument = &entity;
      continue;
    }
    const auto setEntityError = [&error, &entity](std::string_view message) -> void {
      error.assign("entity '");
      error.append(entity.name);
      error.append("': ");
      error.append(message);
    };
    if (entity.modelGuid && entity.primitiveType) {
      setEntityError("cannot specify both modelGuid and primitiveType");
      return false;
    }
    auto effectiveMaterial = entity.material;
    if (entity.materialGuid) {
      std::string materialError;
      if (m_AssetManager == nullptr) {
        materialError = "asset manager is unavailable";
      }
      if (m_AssetManager == nullptr || !m_AssetManager->LoadMaterial(*entity.materialGuid, materialError)) {
        warnings.push_back("entity '" + entity.name + "': material asset " + *entity.materialGuid +
                           " could not be loaded: " + materialError);
      } else if (const auto assetMaterial = m_AssetManager->GetMaterial(*entity.materialGuid)) {
        effectiveMaterial = *assetMaterial;
      }
    }
    if (effectiveMaterial) {
      prepareMaterialTextures(*effectiveMaterial, entity.name);
    }
    for (const auto& material : entity.materialOverrides) {
      if (material) {
        prepareMaterialTextures(*material, entity.name);
      }
    }
    if (const auto modelGuid = entity.modelGuid; modelGuid.has_value()) {
      const std::string& guid = modelGuid.value();
      std::string assetError;
      if (!m_AssetManager->LoadModel(guid, assetError)) {
        setEntityError(assetError);
        return false;
      }
      if (m_RenderSystem != nullptr) {
        const auto model = m_AssetManager->GetModel(guid);
        const auto generation = m_AssetManager->GetGeneration(guid);
        if (model && generation.has_value() &&
            !m_RenderSystem->PrepareModelResources(guid, generation.value(), *model, {}, error)) {
          const std::string resourceError = error;
          setEntityError("model resource setup failed: ");
          error += resourceError;
          return false;
        }
      }
    }
    const auto primitiveType = entity.primitiveType;
    if (primitiveType.has_value() && !IsPrimitiveType(primitiveType.value())) {
      setEntityError("uses an unsupported primitive type");
      return false;
    }
    if (primitiveType.has_value() && m_RenderSystem != nullptr) {
      const std::string& type = primitiveType.value();
      PrimitiveShape shape = PrimitiveShape::Cone;
      if (type == "cube") {
        shape = PrimitiveShape::Cube;
      } else if (type == "sphere") {
        shape = PrimitiveShape::Sphere;
      } else if (type == "plane") {
        shape = PrimitiveShape::Plane;
      }
      auto material = m_RenderSystem->CreateMaterial({});
      if (!material || !m_RenderSystem->CreatePrimitive(shape, std::move(material))) {
        setEntityError("primitive resource setup failed");
        return false;
      }
    }
    (void)stagedScene->QueueCreate(entity.name);
    entitiesToCreate.push_back(&entity);
  }
  if (document.schemaVersion == 1) {
    for (const auto& light : document.lights) {
      (void)stagedScene->QueueCreate(light.name);
      lightsToCreate.push_back(&light);
    }
    for (const auto& camera : document.cameras) {
      (void)stagedScene->QueueCreate(camera.name);
      camerasToCreate.push_back(&camera);
    }
  }

  std::vector<PendingEntity> entities;
  std::vector<PendingLight> lights;
  std::vector<PendingCamera> cameras;
  const auto created = stagedScene->sync();
  size_t entityIndex = 0;
  size_t lightIndex = 0;
  size_t cameraIndex = 0;
  for (const auto& result : created) {
    if (!result.Outcome) {
      error = result.Outcome.error().Message;
      return false;
    }
    if (!result.EntityCreated) {
      continue;
    }
    if (entityIndex < entitiesToCreate.size()) {
      entities.push_back({.Handle = result.Entity, .Document = entitiesToCreate.at(entityIndex++)});
    } else if (lightIndex < lightsToCreate.size()) {
      lights.push_back({.Handle = result.Entity, .Document = lightsToCreate.at(lightIndex++)});
    } else if (cameraIndex < camerasToCreate.size()) {
      cameras.push_back({.Handle = result.Entity, .Document = camerasToCreate.at(cameraIndex++)});
    }
  }
  if (rootDocument != nullptr) {
    entities.push_back({.Handle = stagedScene->SceneRootEntity(), .Document = rootDocument});
  }
  if (entityIndex != entitiesToCreate.size() || lightIndex != lightsToCreate.size() ||
      cameraIndex != camerasToCreate.size()) {
    error = "scene entity staging did not produce all requested entities";
    return false;
  }

  if (document.schemaVersion >= 2) {
    std::map<std::pair<uint64_t, uint64_t>, EntityHandle> entityHandles;
    for (const auto& pending : entities) {
      const SceneGuidDocument identity =
          pending.Document->identityGuid.value_or(SceneGuidDocument{.high = 0, .low = pending.Document->id});
      entityHandles.emplace(std::pair<uint64_t, uint64_t>{identity.high, identity.low}, pending.Handle);
    }
    for (const auto& source : document.lights) {
      const SceneGuidDocument identity = source.identityGuid.value_or(SceneGuidDocument{.high = 1, .low = source.id});
      const auto found = entityHandles.find({identity.high, identity.low});
      if (found == entityHandles.end()) {
        error = "scene light references a missing entity GUID";
        return false;
      }
      lights.push_back({.Handle = found->second, .Document = &source});
    }
    for (const auto& source : document.cameras) {
      const SceneGuidDocument identity = source.identityGuid.value_or(SceneGuidDocument{.high = 2, .low = source.id});
      const auto found = entityHandles.find({identity.high, identity.low});
      if (found == entityHandles.end()) {
        error = "scene camera references a missing entity GUID";
        return false;
      }
      cameras.push_back({.Handle = found->second, .Document = &source});
    }
  }

  std::map<std::pair<uint64_t, uint64_t>, EntityHandle> handlesByGuid;
  for (const auto& pending : entities) {
    const SceneGuidDocument identity =
        pending.Document->identityGuid.value_or(SceneGuidDocument{.high = 0, .low = pending.Document->id});
    handlesByGuid.emplace(std::pair<uint64_t, uint64_t>{identity.high, identity.low}, pending.Handle);
  }
  for (const auto& pending : lights) {
    const SceneGuidDocument identity =
        pending.Document->identityGuid.value_or(SceneGuidDocument{.high = 1, .low = pending.Document->id});
    handlesByGuid.emplace(std::pair<uint64_t, uint64_t>{identity.high, identity.low}, pending.Handle);
  }
  for (const auto& pending : cameras) {
    const SceneGuidDocument identity =
        pending.Document->identityGuid.value_or(SceneGuidDocument{.high = 2, .low = pending.Document->id});
    handlesByGuid.emplace(std::pair<uint64_t, uint64_t>{identity.high, identity.low}, pending.Handle);
  }
  const auto queueParent = [&stagedScene, &handlesByGuid,
                            &error](EntityHandle child, const std::optional<SceneGuidDocument>& parentGuid) -> bool {
    if (!parentGuid) {
      return true;
    }
    const auto found = handlesByGuid.find({parentGuid->high, parentGuid->low});
    if (found == handlesByGuid.end()) {
      error = "scene hierarchy references a missing parent GUID";
      return false;
    }
    (void)stagedScene->QueueReparent(child, found->second);
    return true;
  };

  for (const PendingEntity& pending : entities) {
    const SceneEntityDocument& source = *pending.Document;
    if (stagedScene->IsSceneRoot(pending.Handle)) {
      (void)stagedScene->QueueSet(pending.Handle, Ecs::Name{.Value = source.name});
    }
    const SceneGuidDocument entityGuid = source.identityGuid.value_or(SceneGuidDocument{.high = 0, .low = source.id});
    (void)stagedScene->QueueSet(pending.Handle,
                                Ecs::PersistentIdentity{.Guid = {.High = entityGuid.high, .Low = entityGuid.low}});
    if (!stagedScene->IsSceneRoot(pending.Handle)) {
      Ecs::LocalTransform transform{.Position = source.position,
                                    .Rotation = source.rotation,
                                    .Scale = source.scale,
                                    .FlipHorizontal = source.flipHorizontal,
                                    .FlipVertical = source.flipVertical};
      (void)stagedScene->QueueSet(pending.Handle, transform);
      (void)stagedScene->QueueSet(pending.Handle, Ecs::LocalVisibility{.IsVisible = source.visible});
    }
    if (!queueParent(pending.Handle, source.parentGuid)) {
      return false;
    }
    if (source.hasMeshRenderer || source.modelGuid || source.materialGuid || source.primitiveType) {
      (void)stagedScene->QueueAdd(pending.Handle,
                                  Ecs::MeshRenderer{
                                      .ModelGuid = source.modelGuid.value_or(""),
                                      .PrimitiveType = source.primitiveType.value_or(""),
                                      .MaterialGuid = source.materialGuid.value_or(""),
                                      .CastShadows = source.castShadows,
                                      .ReceiveShadows = source.receiveShadows,
                                      .Visible = source.meshVisible,
                                      .Wireframe = source.forceWireframe,
                                      .DisableDepthTest = source.disableDepthTest,
                                      .Material = source.materialGuid && m_AssetManager != nullptr
                                                      ? m_AssetManager->GetMaterial(*source.materialGuid)
                                                            .value_or(source.material.value_or(SceneMaterialDocument{}))
                                                      : source.material,
                                      .MaterialOverrides = source.materialOverrides,
                                  });
    }
  }
  for (const PendingLight& pending : lights) {
    const SceneLightDocument& source = *pending.Document;
    Ecs::LocalTransform transform{.Position = source.position,
                                  .Rotation = source.rotation,
                                  .Scale = source.scale,
                                  .FlipHorizontal = source.flipHorizontal,
                                  .FlipVertical = source.flipVertical};
    const Ecs::LightType lightType = source.type == "point" ? Ecs::LightType::Point
                                     : source.type == "spot"
                                         ? Ecs::LightType::Spot
                                         : (source.usePosition ? Ecs::LightType::Spot : Ecs::LightType::Directional);
    Ecs::Light light{
        .Type = lightType,
        .Direction = source.direction,
        .Color = source.color,
        .Intensity = source.intensity,
        .Range = source.range,
        .InnerAngleDegrees = source.innerAngleDegrees,
        .OuterAngleDegrees = source.outerAngleDegrees,
        .ShadowBias = source.shadowBias,
        .ShadowNormalBias = source.shadowNormalBias,
        .CastShadows = source.castShadows,
    };
    (void)stagedScene->QueueSet(pending.Handle, transform);
    const SceneGuidDocument lightGuid = source.identityGuid.value_or(SceneGuidDocument{.high = 1, .low = source.id});
    (void)stagedScene->QueueSet(pending.Handle,
                                Ecs::PersistentIdentity{.Guid = {.High = lightGuid.high, .Low = lightGuid.low}});
    (void)stagedScene->QueueSet(pending.Handle, Ecs::LocalVisibility{.IsVisible = source.visible});
    if (!queueParent(pending.Handle, source.parentGuid)) {
      return false;
    }
    (void)stagedScene->QueueAdd(pending.Handle, light);
  }
  for (const PendingCamera& pending : cameras) {
    const SceneCameraDocument& source = *pending.Document;
    Ecs::LocalTransform transform{.Position = source.position,
                                  .Rotation = source.rotation,
                                  .Scale = source.scale,
                                  .FlipHorizontal = source.flipHorizontal,
                                  .FlipVertical = source.flipVertical};
    Ecs::Camera camera{
        .FieldOfViewDegrees = DirectX::XMConvertToDegrees(source.fovRadians),
        .NearPlane = source.nearPlane,
        .FarPlane = source.farPlane,
        .OutputWidth = source.outputWidth,
        .OutputHeight = source.outputHeight,
        .Priority = source.priority,
        .Active = source.active,
    };
    (void)stagedScene->QueueSet(pending.Handle, transform);
    const SceneGuidDocument cameraGuid = source.identityGuid.value_or(SceneGuidDocument{.high = 2, .low = source.id});
    (void)stagedScene->QueueSet(pending.Handle,
                                Ecs::PersistentIdentity{.Guid = {.High = cameraGuid.high, .Low = cameraGuid.low}});
    (void)stagedScene->QueueSet(pending.Handle, Ecs::LocalVisibility{.IsVisible = source.visible});
    if (!queueParent(pending.Handle, source.parentGuid)) {
      return false;
    }
    (void)stagedScene->QueueAdd(pending.Handle, camera);
  }
  for (const auto& result : stagedScene->sync()) {
    if (!result.Outcome) {
      error = result.Outcome.error().Message;
      return false;
    }
  }
  if (rootDocument == nullptr) {
    (void)stagedScene->QueueSet(stagedScene->SceneRootEntity(), Ecs::Name{.Value = "Scene"});
    for (const auto& result : stagedScene->sync()) {
      if (!result.Outcome) {
        error = result.Outcome.error().Message;
        return false;
      }
    }
  }

  m_ActiveViews.clear();
  m_ActiveScenes.clear();
  m_ActiveScenes.push_back(stagedScene);
  OnSceneReplaced(stagedScene);
  ENGINE_LOG_INFO("Core", "Active ECS scene replaced | entities: {} | lights: {} | cameras: {}",
                  document.entities.size(), document.lights.size(), document.cameras.size());
  return true;
}
auto Application::SaveScene(const std::filesystem::path& path, const Scene& scene, std::string& error) const -> bool {
  Log::ScopedOperation operation;
  if (m_AssetManager->SaveScene(path, CreateSceneDocument(scene), error)) {
    return true;
  }
  ENGINE_LOG_ERROR_CODE("Asset", "asset.scene.save_failed", "Scene save failed | path: {} | error: {}",
                        path.generic_string(), error);
  return false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto Application::CreateSceneDocument(const Scene& scene) const -> SceneDocument {
  SceneDocument document;
  document.unitsPerMeter = scene.GetUnitsPerMeter();
  const auto& world = scene.World();
  world.ForEachChunkReadOnly<Ecs::Name, Ecs::PersistentIdentity, Ecs::LocalTransform, Ecs::LocalVisibility>(
      [&document, &world](std::span<const EntityHandle> handles, std::span<const Ecs::Name> names,
                          std::span<const Ecs::PersistentIdentity> identities,
                          std::span<const Ecs::LocalTransform> transforms,
                          std::span<const Ecs::LocalVisibility> visibility) -> void {
        for (size_t row = 0; row < handles.size(); ++row) {
          const auto* mesh = world.Get<Ecs::MeshRenderer>(handles[row]);
          SceneEntityDocument entity;
          entity.id = identities[row].Guid.Low;
          entity.identityGuid = SceneGuidDocument{.high = identities[row].Guid.High, .low = identities[row].Guid.Low};
          entity.name = names[row].Value;
          entity.sceneRoot = world.Get<Ecs::SceneRoot>(handles[row]) != nullptr;
          if (!entity.sceneRoot) {
            entity.visible = visibility[row].IsVisible;
            entity.position = transforms[row].Position;
            entity.rotation = transforms[row].Rotation;
            entity.scale = transforms[row].Scale;
            entity.flipHorizontal = transforms[row].FlipHorizontal;
            entity.flipVertical = transforms[row].FlipVertical;
          }
          if (const auto* parent = world.Get<Ecs::Parent>(handles[row]);
              parent != nullptr && parent->Id != InvalidEntityHandle) {
            if (const auto* identity = world.Get<Ecs::PersistentIdentity>(parent->Id); identity != nullptr) {
              entity.parentGuid = SceneGuidDocument{.high = identity->Guid.High, .low = identity->Guid.Low};
            }
          }
          if (mesh != nullptr && !mesh->ModelGuid.empty()) {
            entity.modelGuid = mesh->ModelGuid;
          }
          if (mesh != nullptr && !mesh->PrimitiveType.empty()) {
            entity.primitiveType = mesh->PrimitiveType;
          }
          if (mesh != nullptr && !mesh->MaterialGuid.empty()) {
            entity.materialGuid = mesh->MaterialGuid;
          }
          if (mesh != nullptr) {
            entity.hasMeshRenderer = true;
            entity.forceWireframe = mesh->Wireframe;
            entity.meshVisible = mesh->Visible;
            entity.disableDepthTest = mesh->DisableDepthTest;
            entity.castShadows = mesh->CastShadows;
            entity.receiveShadows = mesh->ReceiveShadows;
            entity.material = mesh->Material;
            entity.materialOverrides = mesh->MaterialOverrides;
          }
          document.entities.push_back(std::move(entity));
        }
      });

  world.ForEachChunkReadOnly<Ecs::Name, Ecs::PersistentIdentity, Ecs::LocalTransform, Ecs::LocalVisibility,
                             Ecs::SpotLight>(
      [&document,
       &world](std::span<const EntityHandle> handles, std::span<const Ecs::Name> names,
               std::span<const Ecs::PersistentIdentity> identities, std::span<const Ecs::LocalTransform> transforms,
               std::span<const Ecs::LocalVisibility> visibility, std::span<const Ecs::SpotLight> components) -> void {
        for (size_t row = 0; row < handles.size(); ++row) {
          SceneLightDocument light;
          light.id = identities[row].Guid.Low;
          light.name = names[row].Value;
          light.identityGuid = SceneGuidDocument{.high = identities[row].Guid.High, .low = identities[row].Guid.Low};
          light.visible = visibility[row].IsVisible;
          if (const auto* parent = world.Get<Ecs::Parent>(handles[row]);
              parent != nullptr && parent->Id != InvalidEntityHandle) {
            if (const auto* identity = world.Get<Ecs::PersistentIdentity>(parent->Id); identity != nullptr) {
              light.parentGuid = SceneGuidDocument{.high = identity->Guid.High, .low = identity->Guid.Low};
            }
          }
          light.direction = components[row].Direction;
          light.position = transforms[row].Position;
          light.rotation = transforms[row].Rotation;
          light.scale = transforms[row].Scale;
          light.flipHorizontal = transforms[row].FlipHorizontal;
          light.flipVertical = transforms[row].FlipVertical;
          light.color = components[row].Color;
          light.intensity = components[row].Intensity;
          light.range = components[row].Range;
          light.innerAngleDegrees = components[row].InnerAngleDegrees;
          light.outerAngleDegrees = components[row].OuterAngleDegrees;
          light.shadowBias = components[row].ShadowBias;
          light.shadowNormalBias = components[row].ShadowNormalBias;
          light.type = components[row].Type == Ecs::LightType::Directional ? "directional"
                       : components[row].Type == Ecs::LightType::Point     ? "point"
                                                                           : "spot";
          light.usePosition = components[row].Type != Ecs::LightType::Directional;
          light.castShadows = components[row].CastShadows;
          document.lights.push_back(light);
        }
      });

  world
      .ForEachChunkReadOnly<Ecs::Name, Ecs::PersistentIdentity, Ecs::LocalTransform, Ecs::LocalVisibility, Ecs::Camera>(
          [&document,
           &world](std::span<const EntityHandle> handles, std::span<const Ecs::Name> names,
                   std::span<const Ecs::PersistentIdentity> identities, std::span<const Ecs::LocalTransform> transforms,
                   std::span<const Ecs::LocalVisibility> visibility, std::span<const Ecs::Camera> components) -> void {
            for (size_t row = 0; row < handles.size(); ++row) {
              SceneCameraDocument camera;
              camera.id = identities[row].Guid.Low;
              camera.name = names[row].Value;
              camera.identityGuid =
                  SceneGuidDocument{.high = identities[row].Guid.High, .low = identities[row].Guid.Low};
              camera.visible = visibility[row].IsVisible;
              if (const auto* parent = world.Get<Ecs::Parent>(handles[row]);
                  parent != nullptr && parent->Id != InvalidEntityHandle) {
                if (const auto* identity = world.Get<Ecs::PersistentIdentity>(parent->Id); identity != nullptr) {
                  camera.parentGuid = SceneGuidDocument{.high = identity->Guid.High, .low = identity->Guid.Low};
                }
              }
              camera.position = transforms[row].Position;
              camera.rotation = transforms[row].Rotation;
              camera.scale = transforms[row].Scale;
              camera.flipHorizontal = transforms[row].FlipHorizontal;
              camera.flipVertical = transforms[row].FlipVertical;
              camera.fovRadians = DirectX::XMConvertToRadians(components[row].FieldOfViewDegrees);
              camera.nearPlane = components[row].NearPlane;
              camera.farPlane = components[row].FarPlane;
              camera.outputWidth = components[row].OutputWidth;
              camera.outputHeight = components[row].OutputHeight;
              camera.priority = components[row].Priority;
              camera.active = components[row].Active;
              document.cameras.push_back(camera);
            }
          });
  return document;
}
void Application::AddViewport(const std::shared_ptr<Engine::Viewport>& viewport) { m_Viewports.push_back(viewport); }

void Application::RemoveViewport(const std::shared_ptr<Engine::Viewport>& viewport) {
  auto it = std::ranges::find(m_Viewports, viewport);
  if (it != m_Viewports.end()) {
    m_Viewports.erase(it);
  }
}

void Application::RegisterView(const ViewportBinding& binding) {
  if (binding.IsValid()) {
    m_ActiveViews.push_back(binding);
  }
}

auto Application::GetWindow() const -> IWindow* { return m_Window.get(); }
} // namespace Engine
