#include "runtime/include/Application.h"

#include "assets/include/AssetManager.h"
#include "rendering/include/RenderBackend.h"
#include "rendering/include/renderer/Camera.h"
#include "rendering/include/renderer/Entity.h"
#include "rendering/include/renderer/Light.h"
#include "runtime/include/Window.h"
#include "scene/include/Scene.h"
#include "scene/include/SceneDocument.h"
#include "../../support/WarpRenderBackend.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

void WriteTriangleModel(const std::filesystem::path &path)
{
    const nlohmann::json model = {
        {"asset", {{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", nlohmann::json::array({{{"nodes", nlohmann::json::array({0})}}})},
        {"nodes", nlohmann::json::array({{{"name", "triangle"}, {"mesh", 0}}})},
        {"meshes",
         nlohmann::json::array(
             {{{"primitives",
                nlohmann::json::array(
                    {{{"attributes", {{"POSITION", 0}}}, {"indices", 1}, {"material", 0}, {"mode", 4}}})}}})},
        {"materials", nlohmann::json::array({{{"name", "override target"}}})},
        {"buffers", nlohmann::json::array({{{"uri", "application-triangle.bin"}, {"byteLength", 42}}})},
        {"bufferViews", nlohmann::json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
                                               {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 6}}})},
        {"accessors", nlohmann::json::array(
                          {{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
                           {{"bufferView", 1}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}}})}};
    const std::array<float, 9> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::array<uint16_t, 3> indices = {0, 1, 2};
    std::ofstream buffer(path.parent_path() / "application-triangle.bin", std::ios::binary | std::ios::trunc);
    buffer.write(reinterpret_cast<const char *>(positions.data()), sizeof(positions));
    buffer.write(reinterpret_cast<const char *>(indices.data()), sizeof(indices));
    ASSERT_TRUE(buffer.good());

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << model.dump(2);
    ASSERT_TRUE(output.good());
}

struct BackendState
{
    bool initializeResult = true;
    int initializeCalls = 0;
    int presentCalls = 0;
    int resizeCalls = 0;
};

class TestWindow final : public Engine::IWindow
{
  public:
    explicit TestWindow(uint32_t closeAfterPollCount = 0)
        : m_ShouldClose(closeAfterPollCount == 0), m_CloseAfterPollCount(closeAfterPollCount)
    {
    }

    void *GetNativeHandle() override
    {
        return this;
    }

    void *GetEventSourceHandle() const override
    {
        return const_cast<TestWindow *>(this);
    }

    bool ShouldClose() const override
    {
        return m_ShouldClose;
    }

    void PollEvents() override
    {
        if (m_CloseAfterPollCount > 0)
        {
            m_Width = 800;
            m_Height = 600;
            --m_CloseAfterPollCount;
            m_ShouldClose = m_CloseAfterPollCount == 0;
        }
    }

    void SetEventCallback(Engine::Events::EventCallbackFn) override
    {
    }

    void SetCursorVisible(bool) override
    {
    }

    uint32_t GetWidth() const override
    {
        return m_Width;
    }

    uint32_t GetHeight() const override
    {
        return m_Height;
    }

  private:
    bool m_ShouldClose = true;
    uint32_t m_CloseAfterPollCount = 0;
    uint32_t m_Width = 640;
    uint32_t m_Height = 480;
};

class TimingProbeApplication final : public Engine::Application
{
  public:
    std::vector<float> updateDeltas;
    uint32_t resizedWidth = 0;
    uint32_t resizedHeight = 0;
    uint32_t shutdownCount = 0;

  protected:
    void OnUpdate(float deltaTime) override
    {
        updateDeltas.push_back(deltaTime);
        if (updateDeltas.size() == 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(130));
    }
    void OnResize(uint32_t width, uint32_t height) override
    {
        resizedWidth = width;
        resizedHeight = height;
    }
    void OnShutdownUI() override
    {
        ++shutdownCount;
    }
};

class TestRenderBackend final : public Engine::IRenderBackend
{
  public:
    explicit TestRenderBackend(std::shared_ptr<BackendState> state) : m_State(std::move(state))
    {
    }

    bool Initialize(Engine::IWindow *) override
    {
        ++m_State->initializeCalls;
        return m_State->initializeResult;
    }

    void Present() override
    {
        ++m_State->presentCalls;
    }

    void Resize(int, int) override
    {
        ++m_State->resizeCalls;
    }

    void *GetDevice() const override
    {
        return nullptr;
    }

    void *GetDefaultFramebuffer() const override
    {
        return nullptr;
    }

  private:
    std::shared_ptr<BackendState> m_State;
};

TEST(ApplicationTests, RejectsMissingWindowAndBackend)
{
    Engine::Application application;

    application.Run(nullptr, nullptr);

    EXPECT_EQ(application.GetFrameTimingStats().FrameCount, 0U);
    EXPECT_EQ(application.GetFrameMetricsCount(), 0U);
}

TEST(ApplicationTests, StopsWhenBackendInitializationFails)
{
    auto state = std::make_shared<BackendState>();
    state->initializeResult = false;
    Engine::Application application;

    application.Run(std::make_unique<TestWindow>(), std::make_unique<TestRenderBackend>(state));

    EXPECT_EQ(state->initializeCalls, 1);
    EXPECT_EQ(state->presentCalls, 0);
    EXPECT_EQ(application.GetFrameTimingStats().FrameCount, 0U);
}

TEST(ApplicationTests, RejectsRendererWithoutDevice)
{
    auto state = std::make_shared<BackendState>();
    Engine::Application application;

    application.Run(std::make_unique<TestWindow>(), std::make_unique<TestRenderBackend>(state));

    EXPECT_EQ(state->initializeCalls, 1);
    EXPECT_EQ(state->presentCalls, 0);
    EXPECT_EQ(application.GetFrameTimingStats().FrameCount, 0U);
}

TEST(ApplicationTests, RejectsSceneDocumentUntilRendererIsInitialized)
{
    Engine::Application application;
    Engine::SceneDocument document;
    std::string error = "stale error";
    std::vector<std::string> warnings{"stale warning"};

    EXPECT_FALSE(application.LoadSceneDocument(document, error, warnings));
    EXPECT_EQ(error, "scene loading requires an initialized renderer outside an active frame");
    EXPECT_EQ(warnings, std::vector<std::string>{"stale warning"});
}

TEST(ApplicationTests, AddsAndRemovesScenesBySharedPointer)
{
    Engine::Application application;
    auto scene = std::make_shared<Engine::Scene>();

    application.AddScene(scene);
    application.AddScene(nullptr);
    ASSERT_EQ(application.GetScenes().size(), 2U);
    EXPECT_EQ(application.GetScenes()[0], scene);
    EXPECT_EQ(application.GetScenes()[1], nullptr);

    application.RemoveScene(scene);
    application.RemoveScene(scene);
    ASSERT_EQ(application.GetScenes().size(), 1U);
    EXPECT_EQ(application.GetScenes()[0], nullptr);
}

TEST(ApplicationTests, RunsWarpFrameLoadsAndSavesSceneDocuments)
{
    EngineTests::WarpDevice device;
    ASSERT_TRUE(device.Initialize())
        << "The D3D11 WARP software device is required for this integration test.";

    Engine::Application application;
    auto initialScene = std::make_shared<Engine::Scene>();
    initialScene->AddCamera(std::make_shared<Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f));
    application.AddScene(initialScene);
    application.Run(std::make_unique<TestWindow>(true),
                    std::make_unique<EngineTests::WarpRenderBackend>(device.GetDevice()));

    ASSERT_NE(application.GetRenderSystem(), nullptr);
    ASSERT_NE(application.GetWindow(), nullptr);
    EXPECT_EQ(application.GetWindow()->GetWidth(), 800U);
    EXPECT_EQ(application.GetWindow()->GetHeight(), 600U);
    EXPECT_EQ(application.GetFrameTimingStats().FrameCount, 1U);
    EXPECT_EQ(application.GetFrameMetricsCount(), 1U);
    EXPECT_EQ(application.GetFrameMetricsHistory()[0].FrameIndex, 0U);

    Engine::SceneDocument document;
    document.unitsPerMeter = 0.5f;
    Engine::SceneEntityDocument rootEntity;
    rootEntity.id = 15;
    rootEntity.name = "scene root child";
    rootEntity.primitiveType = "cube";
    document.entities.push_back(rootEntity);
    Engine::SceneEntityDocument parentEntity;
    parentEntity.id = 16;
    parentEntity.name = "parent cube";
    parentEntity.primitiveType = "cube";
    parentEntity.parent = Engine::SceneNodeParentDocument{"entity", 15, 0};
    Engine::SceneMaterialDocument maskedMaterial;
    maskedMaterial.brdfModel = "ggx";
    maskedMaterial.specularMode = "explicitF0";
    maskedMaterial.alphaMode = "mask";
    parentEntity.material = maskedMaterial;
    document.entities.push_back(parentEntity);
    Engine::SceneEntityDocument entity;
    entity.id = 17;
    entity.name = "saved cone";
    entity.primitiveType = "cone";
    entity.parentId = 16;
    entity.parent = Engine::SceneNodeParentDocument{"entity", 16, 0};
    entity.position = {1.0f, 2.0f, 3.0f};
    entity.forceWireframe = true;
    Engine::SceneMaterialDocument material;
    material.brdfModel = "gtr";
    material.workflow = "specularGlossiness";
    material.alphaMode = "blend";
    entity.material = material;
    document.entities.push_back(entity);
    Engine::SceneEntityDocument cameraChild;
    cameraChild.id = 18;
    cameraChild.name = "camera child";
    cameraChild.primitiveType = "sphere";
    cameraChild.parent = Engine::SceneNodeParentDocument{"camera", 31, 0};
    document.entities.push_back(cameraChild);
    Engine::SceneEntityDocument legacyParentChild;
    legacyParentChild.id = 19;
    legacyParentChild.name = "legacy parent child";
    legacyParentChild.primitiveType = "plane";
    legacyParentChild.parentId = 15;
    document.entities.push_back(legacyParentChild);

    Engine::SceneLightDocument light;
    light.id = 23;
    light.parent = Engine::SceneNodeParentDocument{"entity", 16, 1};
    light.usePosition = true;
    light.position = {3.0f, 4.0f, 5.0f};
    light.castShadows = false;
    document.lights.push_back(light);

    Engine::SceneCameraDocument camera;
    camera.id = 31;
    camera.parent = Engine::SceneNodeParentDocument{"light", 23, 0};
    camera.position = {0.0f, 1.0f, 5.0f};
    camera.nearPlane = 0.25f;
    camera.farPlane = 250.0f;
    document.cameras.push_back(camera);

    std::string error;
    std::vector<std::string> warnings;
    ASSERT_TRUE(application.LoadSceneDocument(document, error, warnings)) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(warnings.empty()) << (warnings.empty() ? std::string{} : warnings.front());
    ASSERT_EQ(application.GetScenes().size(), 1U);
    auto loadedScene = application.GetScenes().front();
    ASSERT_EQ(loadedScene->GetEntityCount(), 5U);
    ASSERT_EQ(loadedScene->GetLights().size(), 1U);
    ASSERT_EQ(loadedScene->GetCameras().size(), 1U);
    EXPECT_FLOAT_EQ(loadedScene->GetUnitsPerMeter(), 0.5f);
    const auto loadedParentEntity = loadedScene->FindEntity(16);
    const auto loadedRootEntity = loadedScene->FindEntity(15);
    const auto loadedChildEntity = loadedScene->FindEntity(17);
    const auto loadedCameraChild = loadedScene->FindEntity(18);
    const auto loadedLegacyChild = loadedScene->FindEntity(19);
    const auto loadedLight = loadedScene->FindLight(23);
    const auto loadedCamera = loadedScene->FindCamera(31);
    ASSERT_NE(loadedRootEntity, nullptr);
    ASSERT_NE(loadedParentEntity, nullptr);
    ASSERT_NE(loadedChildEntity, nullptr);
    ASSERT_NE(loadedCameraChild, nullptr);
    ASSERT_NE(loadedLegacyChild, nullptr);
    ASSERT_NE(loadedLight, nullptr);
    ASSERT_NE(loadedCamera, nullptr);
    EXPECT_EQ(loadedParentEntity->GetParent(), loadedRootEntity.get());
    EXPECT_EQ(loadedChildEntity->GetParent(), loadedParentEntity.get());
    EXPECT_EQ(loadedCameraChild->GetParent(), loadedCamera.get());
    EXPECT_EQ(loadedLegacyChild->GetParent(), loadedRootEntity.get());
    EXPECT_EQ(loadedLight->GetParent(), loadedParentEntity.get());
    EXPECT_EQ(loadedCamera->GetParent(), loadedLight.get());

    Engine::SceneDocument snapshot = application.CreateSceneDocument(*loadedScene);
    ASSERT_EQ(snapshot.entities.size(), 5U);
    const auto childSnapshot = std::find_if(snapshot.entities.begin(), snapshot.entities.end(),
                                            [](const auto &item) { return item.id == 17; });
    ASSERT_NE(childSnapshot, snapshot.entities.end());
    EXPECT_EQ(childSnapshot->name, "saved cone");
    EXPECT_EQ(childSnapshot->primitiveType, std::optional<std::string>("cone"));
    ASSERT_TRUE(childSnapshot->material.has_value());
    EXPECT_EQ(childSnapshot->material.value_or(Engine::SceneMaterialDocument{}).brdfModel, "gtr");
    ASSERT_TRUE(childSnapshot->parentId.has_value());
    EXPECT_EQ(childSnapshot->parentId.value_or(0), 16U);
    ASSERT_TRUE(childSnapshot->parent.has_value());
    const auto childParent = childSnapshot->parent.value_or(Engine::SceneNodeParentDocument{});
    EXPECT_EQ(childParent.type, "entity");
    EXPECT_EQ(childParent.id, 16U);
    const auto cameraChildSnapshot = std::find_if(snapshot.entities.begin(), snapshot.entities.end(),
                                                  [](const auto &item) { return item.id == 18; });
    ASSERT_NE(cameraChildSnapshot, snapshot.entities.end());
    ASSERT_TRUE(cameraChildSnapshot->parent.has_value());
    EXPECT_EQ(cameraChildSnapshot->parent.value_or(Engine::SceneNodeParentDocument{}).type, "camera");
    const auto legacyChildSnapshot = std::find_if(snapshot.entities.begin(), snapshot.entities.end(),
                                                  [](const auto &item) { return item.id == 19; });
    ASSERT_NE(legacyChildSnapshot, snapshot.entities.end());
    ASSERT_TRUE(legacyChildSnapshot->parentId.has_value());
    EXPECT_EQ(legacyChildSnapshot->parentId.value_or(0), 15U);
    ASSERT_TRUE(snapshot.lights[0].parent.has_value());
    const auto lightParent = snapshot.lights[0].parent.value_or(Engine::SceneNodeParentDocument{});
    EXPECT_EQ(lightParent.type, "entity");
    EXPECT_EQ(lightParent.id, 16U);
    ASSERT_TRUE(snapshot.cameras[0].parent.has_value());
    const auto cameraParent = snapshot.cameras[0].parent.value_or(Engine::SceneNodeParentDocument{});
    EXPECT_EQ(cameraParent.type, "light");
    EXPECT_EQ(cameraParent.id, 23U);
    EXPECT_EQ(snapshot.lights.size(), 1U);
    EXPECT_EQ(snapshot.cameras.size(), 1U);

    const auto directory = std::filesystem::temp_directory_path() /
                           ("GraphicEngineApplicationTests_" +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto scenePath = directory / "application-scene.json";
    ASSERT_TRUE(application.GetAssetManager()->SetProjectRoot(directory, error)) << error;
    ASSERT_TRUE(application.SaveScene(scenePath, *loadedScene, error)) << error;
    EXPECT_TRUE(std::filesystem::exists(scenePath));

    WriteTriangleModel(directory / "application-triangle.gltf");
    const auto modelGuid = application.GetAssetManager()->ImportModel("application-triangle.gltf");
    ASSERT_TRUE(modelGuid) << modelGuid.error().Message;
    Engine::SceneDocument modelDocument;
    Engine::SceneEntityDocument modelEntity;
    modelEntity.id = 82;
    modelEntity.name = "registered model";
    modelEntity.modelGuid = *modelGuid;
    Engine::SceneMaterialDocument overrideMaterial;
    overrideMaterial.baseColorTextureGuid = "missing-model-override-texture";
    modelEntity.materialOverrides.push_back(overrideMaterial);
    modelDocument.entities.push_back(modelEntity);
    warnings.clear();
    ASSERT_TRUE(application.LoadSceneDocument(modelDocument, error, warnings)) << error;
    ASSERT_EQ(application.GetScenes().front()->GetEntityCount(), 1U);
    EXPECT_EQ(application.GetScenes().front()->GetEntities().front()->Submeshes.size(), 1U);
    ASSERT_EQ(warnings.size(), 1U);
    EXPECT_NE(warnings.front().find("material slot 0 texture missing-model-override-texture is unavailable"),
              std::string::npos);

    warnings.clear();
    ASSERT_TRUE(application.LoadScene(scenePath, error, warnings)) << error;
    EXPECT_TRUE(warnings.empty()) << (warnings.empty() ? std::string{} : warnings.front());
    loadedScene = application.GetScenes().front();
    EXPECT_EQ(loadedScene->FindEntity(16)->GetParent(), loadedScene->FindEntity(15).get());
    EXPECT_EQ(loadedScene->FindEntity(17)->GetParent(), loadedScene->FindEntity(16).get());
    EXPECT_EQ(loadedScene->FindEntity(18)->GetParent(), loadedScene->FindCamera(31).get());
    EXPECT_EQ(loadedScene->FindEntity(19)->GetParent(), loadedScene->FindEntity(15).get());
    EXPECT_EQ(loadedScene->FindLight(23)->GetParent(), loadedScene->FindEntity(16).get());
    EXPECT_EQ(loadedScene->FindCamera(31)->GetParent(), loadedScene->FindLight(23).get());

    Engine::SceneDocument invalid = document;
    invalid.entities[0].primitiveType = "not-a-primitive";
    const auto priorScene = application.GetScenes().front();
    EXPECT_FALSE(application.LoadSceneDocument(invalid, error, warnings));
    EXPECT_NE(error.find("unsupported primitive type"), std::string::npos);
    EXPECT_EQ(application.GetScenes().front(), priorScene);

    invalid = document;
    invalid.entities[0].modelGuid = "also-a-model";
    EXPECT_FALSE(application.LoadSceneDocument(invalid, error, warnings));
    EXPECT_NE(error.find("both modelGuid and primitiveType"), std::string::npos);
    EXPECT_EQ(application.GetScenes().front(), priorScene);

    invalid = Engine::SceneDocument{};
    Engine::SceneEntityDocument missingShape;
    missingShape.id = 41;
    missingShape.name = "unshaped entity";
    invalid.entities.push_back(missingShape);
    EXPECT_FALSE(application.LoadSceneDocument(invalid, error, warnings));
    EXPECT_NE(error.find("has neither modelGuid nor primitiveType"), std::string::npos);
    EXPECT_EQ(application.GetScenes().front(), priorScene);

    invalid = Engine::SceneDocument{};
    Engine::SceneEntityDocument standaloneMaterial;
    standaloneMaterial.id = 42;
    standaloneMaterial.name = "unsupported material reference";
    standaloneMaterial.materialGuid = "material-guid";
    invalid.entities.push_back(standaloneMaterial);
    EXPECT_FALSE(application.LoadSceneDocument(invalid, error, warnings));
    EXPECT_NE(error.find("standalone material GUID material-guid"), std::string::npos);
    EXPECT_EQ(application.GetScenes().front(), priorScene);

    invalid = Engine::SceneDocument{};
    Engine::SceneEntityDocument missingModel;
    missingModel.id = 43;
    missingModel.name = "missing model";
    missingModel.modelGuid = "missing-model-guid";
    invalid.entities.push_back(missingModel);
    EXPECT_FALSE(application.LoadSceneDocument(invalid, error, warnings));
    EXPECT_NE(error.find("model missing-model-guid"), std::string::npos);
    EXPECT_EQ(application.GetScenes().front(), priorScene);

    invalid = Engine::SceneDocument{};
    Engine::SceneEntityDocument firstDuplicate;
    firstDuplicate.id = 51;
    firstDuplicate.name = "first duplicate";
    firstDuplicate.primitiveType = "cube";
    invalid.entities.push_back(firstDuplicate);
    auto secondDuplicate = firstDuplicate;
    secondDuplicate.name = "second duplicate";
    invalid.entities.push_back(secondDuplicate);
    EXPECT_FALSE(application.LoadSceneDocument(invalid, error, warnings));
    EXPECT_NE(error.find("duplicate or unsupported entity ID 51"), std::string::npos);
    EXPECT_EQ(application.GetScenes().front(), priorScene);

    invalid = Engine::SceneDocument{};
    Engine::SceneLightDocument firstLight;
    firstLight.id = 61;
    invalid.lights.push_back(firstLight);
    invalid.lights.push_back(firstLight);
    EXPECT_FALSE(application.LoadSceneDocument(invalid, error, warnings));
    EXPECT_NE(error.find("duplicate or unsupported light ID 61"), std::string::npos);
    EXPECT_EQ(application.GetScenes().front(), priorScene);

    invalid = Engine::SceneDocument{};
    Engine::SceneCameraDocument firstCamera;
    firstCamera.id = 71;
    invalid.cameras.push_back(firstCamera);
    invalid.cameras.push_back(firstCamera);
    EXPECT_FALSE(application.LoadSceneDocument(invalid, error, warnings));
    EXPECT_NE(error.find("duplicate or unsupported camera ID 71"), std::string::npos);
    EXPECT_EQ(application.GetScenes().front(), priorScene);

    invalid = Engine::SceneDocument{};
    Engine::SceneEntityDocument unavailableTexture;
    unavailableTexture.id = 81;
    unavailableTexture.name = "missing texture fallback";
    unavailableTexture.primitiveType = "cube";
    Engine::SceneMaterialDocument texturedMaterial;
    texturedMaterial.baseColorTextureGuid = "missing-texture-guid";
    unavailableTexture.material = texturedMaterial;
    invalid.entities.push_back(unavailableTexture);
    warnings.clear();
    ASSERT_TRUE(application.LoadSceneDocument(invalid, error, warnings)) << error;
    ASSERT_EQ(warnings.size(), 1U);
    EXPECT_NE(warnings.front().find("missing-texture-guid is unavailable"), std::string::npos);
    EXPECT_EQ(application.GetScenes().front()->GetEntityCount(), 1U);

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    EXPECT_FALSE(cleanupError);
}

TEST(ApplicationTests, ClampsDelayedFramesAndRunsWhileSimulationIsPaused)
{
    EngineTests::WarpDevice device;
    ASSERT_TRUE(device.Initialize())
        << "The D3D11 WARP software device is required for this integration test.";

    TimingProbeApplication application;
    application.SetSimulationPaused(true);
    application.SetFrameLimiterHz(120);
    application.Run(std::make_unique<TestWindow>(2),
                    std::make_unique<EngineTests::WarpRenderBackend>(device.GetDevice()));

    ASSERT_EQ(application.updateDeltas.size(), 2U);
    EXPECT_LT(application.updateDeltas[0], 0.1f);
    EXPECT_LE(application.updateDeltas[1], 0.1f);
    EXPECT_EQ(application.GetFrameTimingStats().FrameCount, 2U);
    EXPECT_EQ(application.GetFrameTimingStats().ClampedFrameCount, 1U);
    EXPECT_EQ(application.GetFrameTimingStats().GameplayDeltaTime, application.updateDeltas[1]);
    EXPECT_EQ(application.GetFrameLimiterHz(), 120U);
    EXPECT_EQ(application.resizedWidth, 800U);
    EXPECT_EQ(application.resizedHeight, 600U);
    EXPECT_EQ(application.shutdownCount, 1U);
    EXPECT_TRUE(application.IsSimulationPaused());
    EXPECT_GE(application.GetFrameMetricsCount(), 2U);
    EXPECT_EQ(application.GetFrameMetricsHistory()[1].FrameIndex, 1U);
}

} // namespace
