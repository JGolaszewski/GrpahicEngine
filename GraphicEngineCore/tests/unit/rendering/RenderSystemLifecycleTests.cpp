#include "rendering/include/RenderBackend.h"
#include "rendering/include/RenderSystem.h"
#include "rendering/include/renderer/Camera.h"
#include "rendering/include/renderer/Entity.h"
#include "rendering/include/renderer/Light.h"
#include "rendering/include/renderer/Mesh.h"
#include "rendering/include/renderer/Vertex.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/include/renderer/ViewportBinding.h"
#include "scene/include/Scene.h"
#include "../../support/WarpRenderBackend.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

namespace
{
class NullDeviceBackend final : public Engine::IRenderBackend
{
  public:
    bool Initialize(Engine::IWindow *) override
    {
        return true;
    }
    void Present() override
    {
    }
    void Resize(int, int) override
    {
    }
    void *GetDevice() const override
    {
        return nullptr;
    }
    void *GetDefaultFramebuffer() const override
    {
        return nullptr;
    }
};

TEST(RenderSystemLifecycleTests, NullDeviceIsRejectedAndFrameCallsRemainSafe)
{
    NullDeviceBackend backend;
    Engine::RenderSystem renderSystem(&backend);

    EXPECT_FALSE(renderSystem.IsValid());
    EXPECT_FALSE(renderSystem.IsFrameActive());
    EXPECT_EQ(renderSystem.CreateViewportTarget(640, 480), nullptr);

    renderSystem.BeginFrame();
    renderSystem.SubmitViews({});
    renderSystem.EndFrame();

    EXPECT_FALSE(renderSystem.IsFrameActive());
    EXPECT_EQ(renderSystem.GetSubmittedViewCount(), 0U);
    EXPECT_EQ(renderSystem.GetFrameDiagnostics().DrawCalls, 0U);
    EXPECT_FALSE(renderSystem.GetGpuFrameTiming().Available);
}

TEST(RenderSystemLifecycleTests, AcceptsValidAndRejectsOutOfRangeShadowSettings)
{
    Engine::RenderSystem renderSystem(nullptr);
    Engine::ShadowSettings valid;
    valid.Resolution = 1024;
    valid.MaxShadowCastersPerScene = 2;

    ASSERT_TRUE(renderSystem.SetShadowSettings(valid));
    EXPECT_EQ(renderSystem.GetShadowSettings().Resolution, 1024U);
    EXPECT_EQ(renderSystem.GetShadowSettings().MaxShadowCastersPerScene, 2U);

    Engine::ShadowSettings invalid = valid;
    invalid.Resolution = 64;
    EXPECT_FALSE(renderSystem.SetShadowSettings(invalid));
    EXPECT_EQ(renderSystem.GetShadowSettings().Resolution, 1024U);

    invalid = valid;
    invalid.MaxShadowCastersPerScene = Engine::MaxShadowedLights + 1;
    EXPECT_FALSE(renderSystem.SetShadowSettings(invalid));
    EXPECT_EQ(renderSystem.GetShadowSettings().MaxShadowCastersPerScene, 2U);

    invalid = valid;
    invalid.Resolution = 8193;
    EXPECT_FALSE(renderSystem.SetShadowSettings(invalid));
    EXPECT_EQ(renderSystem.GetShadowSettings().Resolution, 1024U);
}

TEST(RenderSystemLifecycleTests, InitializesOnWarpUploadsResourcesAndCompletesFrame)
{
    EngineTests::WarpDevice device;
    ASSERT_TRUE(device.Initialize())
        << "The D3D11 WARP software device is required for this integration test.";

    EngineTests::WarpRenderBackend backend(device.GetDevice());
    Engine::RenderSystem renderSystem(&backend);
    ASSERT_TRUE(renderSystem.IsValid());

    auto target = renderSystem.CreateViewportTarget(64, 64);
    ASSERT_NE(target, nullptr);
    EXPECT_TRUE(target->IsValid());
    EXPECT_EQ(target->GetWidth(), 64U);
    EXPECT_EQ(target->GetHeight(), 64U);
    EXPECT_EQ(renderSystem.CreateViewportTarget(0, 64), nullptr);

    Engine::Renderer::MaterialProperties materialProperties;
    auto material = renderSystem.CreateMaterial(materialProperties);
    ASSERT_NE(material, nullptr);

    const std::vector<std::pair<Engine::PrimitiveShape, std::string>> primitiveCases{
        {Engine::PrimitiveShape::Cube, "cube"},
        {Engine::PrimitiveShape::Sphere, "sphere"},
        {Engine::PrimitiveShape::Plane, "plane"},
        {Engine::PrimitiveShape::Cone, "cone"}};
    std::vector<std::shared_ptr<Engine::Renderer::Entity>> primitiveEntities;
    for (const auto &[shape, name] : primitiveCases)
    {
        auto primitiveEntity = renderSystem.CreatePrimitive(shape, material, {0.0f, 0.0f, 3.0f});
        ASSERT_NE(primitiveEntity, nullptr) << name;
        ASSERT_TRUE(primitiveEntity->PrimitiveType.has_value());
        EXPECT_EQ(*primitiveEntity->PrimitiveType, name);
        EXPECT_TRUE(primitiveEntity->mesh->IsValid());
        primitiveEntities.push_back(std::move(primitiveEntity));
    }
    EXPECT_EQ(
        renderSystem.CreatePrimitive(static_cast<Engine::PrimitiveShape>(255), material, {0.0f, 0.0f, 0.0f}),
        nullptr);
    EXPECT_EQ(renderSystem.CreatePrimitive(Engine::PrimitiveShape::Cube, nullptr, {0.0f, 0.0f, 0.0f}),
              nullptr);

    const std::vector<Engine::Renderer::Vertex> vertices{
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.0f}}};
    std::string error;
    auto mesh = renderSystem.GetOrCreateMesh("warp-mesh", 1, vertices, {0, 1, 2}, error);
    ASSERT_NE(mesh, nullptr) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(mesh->GetVertexCount(), 3U);
    EXPECT_EQ(mesh->GetIndexCount(), 3U);
    EXPECT_EQ(renderSystem.GetOrCreateMesh("warp-mesh", 1, vertices, {0, 1, 2}, error), mesh);
    EXPECT_EQ(renderSystem.GetOrCreateMesh("", 1, vertices, {0, 1, 2}, error), nullptr);
    EXPECT_NE(error.find("non-empty asset GUID"), std::string::npos);
    EXPECT_EQ(renderSystem.GetOrCreateMesh("invalid-index-mesh", 1, vertices, {0, 1, 3}, error), nullptr);
    EXPECT_NE(error.find("outside the supplied vertex data"), std::string::npos);
    EXPECT_EQ(renderSystem.GetOrCreateMesh("empty-mesh", 1, {}, {}, error), nullptr);
    EXPECT_NE(error.find("non-empty vertices"), std::string::npos);
    auto replacementMesh = renderSystem.GetOrCreateMesh("warp-mesh", 2, vertices, {0, 1, 2}, error);
    ASSERT_NE(replacementMesh, nullptr) << error;
    EXPECT_NE(replacementMesh, mesh);

    Engine::ImportedImage image;
    image.width = 2;
    image.height = 2;
    image.rgba8 = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    auto texture = renderSystem.GetOrCreateTexture("warp-texture", 1, image, true, error);
    ASSERT_NE(texture, nullptr) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(renderSystem.GetOrCreateTexture("warp-texture", 1, image, true, error), texture);
    EXPECT_EQ(renderSystem.GetOrCreateTexture("", 1, image, true, error), nullptr);
    EXPECT_NE(error.find("non-empty asset or subresource identity"), std::string::npos);
    Engine::ImportedImage invalidImage = image;
    invalidImage.rgba8.pop_back();
    EXPECT_EQ(renderSystem.GetOrCreateTexture("invalid-warp-texture", 1, invalidImage, true, error), nullptr);
    EXPECT_NE(error.find("valid RGBA8 pixels"), std::string::npos);
    auto replacementTexture = renderSystem.GetOrCreateTexture("warp-texture", 2, image, true, error);
    ASSERT_NE(replacementTexture, nullptr) << error;
    EXPECT_NE(replacementTexture, texture);
    renderSystem.ReleaseResource("warp-mesh");
    renderSystem.ReleaseResource("warp-texture");

    Engine::ImportedPrimitive primitive;
    primitive.vertices = vertices;
    primitive.indices = {0, 1, 2};
    primitive.materialIndex = 0;
    primitive.boundsMin = {-0.5f, -0.5f, 0.0f};
    primitive.boundsMax = {0.5f, 0.5f, 0.0f};
    Engine::ImportedMesh importedMesh;
    importedMesh.primitives.push_back(primitive);
    primitive.materialIndex = 1;
    importedMesh.primitives.push_back(primitive);

    Engine::ImportedNode rootNode;
    rootNode.name = "root";
    rootNode.children = {1};
    Engine::ImportedNode meshNode;
    meshNode.name = "triangle";
    meshNode.parentIndex = 0;
    meshNode.meshIndex = 0;
    meshNode.translation = {1.0f, 0.0f, 0.0f};

    Engine::ImportedMaterial importedMaterial;
    importedMaterial.baseColorTexture = 0;
    importedMaterial.alphaMode = "MASK";
    importedMaterial.doubleSided = true;
    importedMaterial.baseColorTransform.offset = {0.25f, 0.5f};
    importedMaterial.baseColorTransform.scale = {2.0f, 2.0f};
    Engine::ImportedMaterial glossinessMaterial;
    glossinessMaterial.specularGlossiness = true;
    glossinessMaterial.glossiness = 0.65f;
    glossinessMaterial.alphaMode = "BLEND";
    glossinessMaterial.normalTexture = 0;
    glossinessMaterial.metallicRoughnessTexture = 0;
    glossinessMaterial.occlusionTexture = 0;
    glossinessMaterial.emissiveTexture = 0;
    Engine::ImportedModel importedModel;
    importedModel.defaultScene = 0;
    importedModel.scenes = {{0}};
    importedModel.nodes = {rootNode, meshNode};
    importedModel.meshes = {importedMesh};
    importedModel.materials = {importedMaterial, glossinessMaterial};
    importedModel.textures = {{0}};
    importedModel.images = {image};

    std::vector<std::vector<std::shared_ptr<Engine::Renderer::Mesh>>> rejectedModelMeshes;
    Engine::ImportedModel emptyModel;
    EXPECT_FALSE(renderSystem.UploadModel("empty-model", 1, emptyModel, rejectedModelMeshes, error));
    EXPECT_NE(error.find("at least one mesh"), std::string::npos);
    Engine::ImportedModel meshWithoutPrimitives = importedModel;
    meshWithoutPrimitives.meshes[0].primitives.clear();
    EXPECT_FALSE(renderSystem.UploadModel("mesh-without-primitives", 1, meshWithoutPrimitives,
                                          rejectedModelMeshes, error));
    EXPECT_NE(error.find("contains no primitives"), std::string::npos);

    std::vector<std::string> warnings;
    auto importedEntity = renderSystem.CreateModelEntity("warp-model", 1, importedModel, {}, error, warnings);
    ASSERT_NE(importedEntity, nullptr) << error;
    EXPECT_TRUE(error.empty());
    ASSERT_EQ(importedEntity->Submeshes.size(), 2U);
    EXPECT_EQ(importedEntity->Submeshes.front().MaterialSlot, 0);
    EXPECT_TRUE(importedEntity->material != nullptr);

    std::vector<std::optional<Engine::Renderer::MaterialProperties>> materialOverrides(1);
    materialOverrides.front() = Engine::Renderer::MaterialProperties{};
    auto overriddenEntity = renderSystem.CreateModelEntity("warp-override-model", 2, importedModel,
                                                           materialOverrides, error, warnings);
    ASSERT_NE(overriddenEntity, nullptr) << error;
    EXPECT_EQ(overriddenEntity->Submeshes.size(), 2U);

    Engine::ImportedModel inferredSceneModel = importedModel;
    inferredSceneModel.scenes.clear();
    inferredSceneModel.defaultScene = -1;
    auto inferredSceneEntity =
        renderSystem.CreateModelEntity("warp-inferred-scene", 3, inferredSceneModel, {}, error, warnings);
    ASSERT_NE(inferredSceneEntity, nullptr) << error;
    EXPECT_EQ(inferredSceneEntity->Submeshes.size(), 2U);

    Engine::ImportedModel emptySceneModel = importedModel;
    emptySceneModel.scenes = {{}};
    EXPECT_EQ(renderSystem.CreateModelEntity("empty-scene", 1, emptySceneModel, {}, error, warnings),
              nullptr);
    EXPECT_NE(error.find("no renderable mesh primitives"), std::string::npos);

    Engine::ImportedModel repeatedRootModel = importedModel;
    repeatedRootModel.scenes = {{0, 0}};
    EXPECT_EQ(renderSystem.CreateModelEntity("repeated-root", 1, repeatedRootModel, {}, error, warnings),
              nullptr);
    EXPECT_NE(error.find("invalid or repeated nodes"), std::string::npos);

    Engine::ImportedModel invalidMeshModel = importedModel;
    invalidMeshModel.nodes[1].meshIndex = 3;
    EXPECT_EQ(renderSystem.CreateModelEntity("invalid-mesh-index", 1, invalidMeshModel, {}, error, warnings),
              nullptr);
    EXPECT_NE(error.find("invalid or repeated nodes"), std::string::npos);

    Engine::ImportedModel malformedModel = importedModel;
    malformedModel.defaultScene = 4;
    EXPECT_EQ(renderSystem.CreateModelEntity("invalid-scene", 1, malformedModel, {}, error, warnings),
              nullptr);
    EXPECT_NE(error.find("default scene index"), std::string::npos);

    malformedModel = importedModel;
    malformedModel.nodes[1].children = {0};
    EXPECT_EQ(renderSystem.CreateModelEntity("invalid-hierarchy", 1, malformedModel, {}, error, warnings),
              nullptr);
    EXPECT_NE(error.find("hierarchy"), std::string::npos);

    malformedModel = importedModel;
    malformedModel.materials[0].baseColorTexture = 5;
    EXPECT_EQ(renderSystem.CreateModelEntity("invalid-texture-slot", 1, malformedModel, {}, error, warnings),
              nullptr);
    EXPECT_NE(error.find("invalid texture slot"), std::string::npos);

    malformedModel = importedModel;
    malformedModel.meshes[0].primitives[1].materialIndex = 8;
    EXPECT_EQ(renderSystem.CreateModelEntity("invalid-material-slot", 1, malformedModel, {}, error, warnings),
              nullptr);
    EXPECT_NE(error.find("material slot"), std::string::npos);

    malformedModel = importedModel;
    malformedModel.textures[0].imageIndex = 8;
    EXPECT_EQ(renderSystem.CreateModelEntity("invalid-image-slot", 1, malformedModel, {}, error, warnings),
              nullptr);
    EXPECT_NE(error.find("invalid image slot"), std::string::npos);

    Engine::Scene scene;
    auto entity = std::make_shared<Engine::Renderer::Entity>(mesh, material);
    entity->Name = "WARP triangle";
    entity->transform.Position = {0.0f, 0.0f, 3.0f};
    ASSERT_NE(scene.AddEntity(entity), Engine::InvalidEntityId);
    ASSERT_NE(scene.AddEntity(importedEntity), Engine::InvalidEntityId);
    ASSERT_NE(scene.AddEntity(overriddenEntity), Engine::InvalidEntityId);
    for (const auto &primitiveEntity : primitiveEntities)
    {
        ASSERT_NE(scene.AddEntity(primitiveEntity), Engine::InvalidEntityId);
    }

    auto light = std::make_shared<Engine::Renderer::SpotLight>();
    light->SetUsePosition(true);
    light->SetCastShadows(true);
    const Engine::LightId lightId = scene.AddLight(light);

    auto camera = std::make_shared<Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f);
    ASSERT_NE(scene.AddCamera(camera), Engine::InvalidCameraId);
    Engine::Viewport viewport(target);
    Engine::ViewportBinding binding{camera.get(), &viewport, &scene};
    binding.DebugSettings.ShowLightGizmos = true;
    binding.DebugSettings.ShowWireframePlane = true;
    binding.DebugSettings.SelectedLightId = lightId;

    renderSystem.BeginFrame();
    ASSERT_TRUE(renderSystem.IsFrameActive());
    EXPECT_EQ(renderSystem.CreatePrimitive(Engine::PrimitiveShape::Cube, material, {0.0f, 0.0f, 0.0f}),
              nullptr);
    EXPECT_EQ(renderSystem.CreateMaterial(materialProperties), nullptr);
    EXPECT_EQ(renderSystem.GetOrCreateMesh("active-frame-mesh", 1, vertices, {0, 1, 2}, error), nullptr);
    EXPECT_NE(error.find("outside an active frame"), std::string::npos);
    EXPECT_EQ(renderSystem.GetOrCreateTexture("active-frame-texture", 1, image, true, error), nullptr);
    EXPECT_NE(error.find("outside an active frame"), std::string::npos);
    std::vector<std::vector<std::shared_ptr<Engine::Renderer::Mesh>>> rejectedMeshes;
    EXPECT_FALSE(renderSystem.UploadModel("active-frame-model", 1, importedModel, rejectedMeshes, error));
    EXPECT_TRUE(rejectedMeshes.empty());
    EXPECT_NE(error.find("outside an active frame"), std::string::npos);
    EXPECT_FALSE(renderSystem.SetMaterialTextures(material, texture, nullptr));
    EXPECT_FALSE(renderSystem.SetPbrMaterialTextures(material, texture, nullptr, nullptr, nullptr, nullptr));
    renderSystem.SubmitViews({binding, {}});
    renderSystem.EndFrame();
    renderSystem.EndFrame();

    EXPECT_FALSE(renderSystem.IsFrameActive());
    EXPECT_GE(renderSystem.GetSubmittedViewCount(), 1U);
    EXPECT_EQ(backend.GetDiagnostics().BackendName, std::string("unknown"));
}
} // namespace
