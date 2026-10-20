#include "rendering/src/SceneRenderer.h"

#include <gtest/gtest.h>

#include <memory>

namespace {
auto AddLight(Engine::Scene& scene, Engine::Ecs::LightType type, bool castShadows = true) -> Engine::EntityHandle {
  (void)scene.QueueCreate("Light", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::SpotLight);
  const auto created = scene.sync();
  EXPECT_EQ(created.size(), 1U);
  if (created.empty() || !created.front().Outcome) {
    return Engine::InvalidEntityHandle;
  }
  Engine::Ecs::Light light;
  light.Type = type;
  light.CastShadows = castShadows;
  (void)scene.QueueSet(created.front().Entity, light);
  (void)scene.sync();
  return created.front().Entity;
}
} // namespace

TEST(SceneRendererShadowTests, AllLightTypesAllocateShadowViewsAndRespectCasterLimits) {
  Engine::Scene scene;
  (void)AddLight(scene, Engine::Ecs::LightType::Directional);
  (void)AddLight(scene, Engine::Ecs::LightType::Point);
  (void)AddLight(scene, Engine::Ecs::LightType::Spot);
  (void)AddLight(scene, Engine::Ecs::LightType::Spot, false);

  EXPECT_EQ(Engine::SceneRenderer::CountShadowCasters(&scene, 3), 3U);
  EXPECT_EQ(Engine::SceneRenderer::CountShadowCasters(&scene, 2), 2U);
  EXPECT_EQ(Engine::SceneRenderer::CountShadowViews(&scene, 3), 8U);
  EXPECT_EQ(Engine::SceneRenderer::CountShadowViews(&scene, 2), 7U);
  EXPECT_EQ(Engine::SceneRenderer::CountShadowViews(&scene, 0), 0U);
}

TEST(SceneRendererShadowTests, BuildsDirectionalSpotAndSixPointFaceViewsWithStableLightSlots) {
  Engine::Scene scene;
  const Engine::EntityHandle directional = AddLight(scene, Engine::Ecs::LightType::Directional);
  const Engine::EntityHandle point = AddLight(scene, Engine::Ecs::LightType::Point);
  const Engine::EntityHandle spot = AddLight(scene, Engine::Ecs::LightType::Spot);
  ASSERT_TRUE(scene.World().IsAlive(directional));
  ASSERT_TRUE(scene.World().IsAlive(point));
  ASSERT_TRUE(scene.World().IsAlive(spot));

  std::vector<std::unique_ptr<Engine::Renderer::RenderView>> ownedViews;
  std::vector<Engine::Renderer::RenderView*> views;
  for (uint32_t i = 0; i < Engine::SceneRenderer::CountShadowViews(&scene, 3); ++i) {
    ownedViews.push_back(std::make_unique<Engine::Renderer::RenderView>());
    ownedViews.back()->ViewType = Engine::Renderer::RenderView::Type::Shadow;
    views.push_back(ownedViews.back().get());
  }
  Engine::SceneRenderer::BuildShadowData(&scene, views, 0, 3);
  for (const auto* view : views) {
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->TargetScene, &scene);
  }
  EXPECT_EQ(views[0]->ShadowLight, directional);
  for (size_t i = 1; i < 7; ++i) {
    EXPECT_EQ(views[i]->ShadowLight, point);
  }
  EXPECT_EQ(views[7]->ShadowLight, spot);
  EXPECT_FALSE(DirectX::XMVector4EqualInt(views[1]->ViewProjection.r[0], views[2]->ViewProjection.r[0]) &&
               DirectX::XMVector4EqualInt(views[1]->ViewProjection.r[1], views[2]->ViewProjection.r[1]) &&
               DirectX::XMVector4EqualInt(views[1]->ViewProjection.r[2], views[2]->ViewProjection.r[2]) &&
               DirectX::XMVector4EqualInt(views[1]->ViewProjection.r[3], views[2]->ViewProjection.r[3]));

  std::vector<Engine::Renderer::DirectionalLightData> lights;
  Engine::SceneRenderer::BuildLighting(&scene, views, lights);
  ASSERT_EQ(lights.size(), 3U);
  EXPECT_FLOAT_EQ(lights[0].ShadowParams.x, 0.0F);
  EXPECT_FLOAT_EQ(lights[1].ShadowParams.x, 1.0F);
  EXPECT_FLOAT_EQ(lights[2].ShadowParams.x, 7.0F);
  EXPECT_FLOAT_EQ(lights[1].ConeParams.z, 1.0F);
  EXPECT_FLOAT_EQ(lights[0].ConeParams.z, 0.0F);
}

TEST(SceneRendererShadowTests, DirectionalProjectionUsesCameraFrustumAndReportsFitDiagnostics) {
  Engine::Scene scene;
  const Engine::EntityHandle light = AddLight(scene, Engine::Ecs::LightType::Directional);
  Engine::Renderer::RenderView cameraView;
  cameraView.CameraPosition = {0.0F, 0.0F, -10.0F};
  cameraView.CameraForward = {0.0F, 0.0F, 1.0F};
  cameraView.CameraRight = {1.0F, 0.0F, 0.0F};
  cameraView.CameraUp = {0.0F, 1.0F, 0.0F};
  cameraView.CameraVerticalFovRadians = DirectX::XMConvertToRadians(60.0F);
  cameraView.CameraAspectRatio = 16.0F / 9.0F;
  cameraView.CameraNearPlane = 0.1F;
  cameraView.CameraFarPlane = 500.0F;
  std::vector<Engine::Renderer::RenderView*> sceneViews{&cameraView};

  Engine::Renderer::RenderView shadowView;
  shadowView.ViewType = Engine::Renderer::RenderView::Type::Shadow;
  std::vector<Engine::Renderer::RenderView*> shadowViews{&shadowView};
  std::vector<Engine::Renderer::DirectionalShadowDiagnostics> diagnostics;
  Engine::SceneRenderer::BuildShadowData(&scene, shadowViews, 0, 1, nullptr, sceneViews, 1024, &diagnostics);

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics[0].Light, light);
  EXPECT_EQ(diagnostics[0].MapResolution, 1024U);
  EXPECT_GT(diagnostics[0].CoverageWidth, 0.0F);
  EXPECT_GT(diagnostics[0].CoverageDepth, 100.0F);
  EXPECT_GT(diagnostics[0].TexelWorldSize, 0.0F);
  EXPECT_FLOAT_EQ(diagnostics[0].Bias, 0.0015F);
}

TEST(SceneRendererShadowTests, DirectionalProjectionIgnoresGeometryOutsideSceneCameras) {
  Engine::Scene scene;
  (void)AddLight(scene, Engine::Ecs::LightType::Directional);
  Engine::Renderer::ModelResourceLibrary resources;
  Engine::Renderer::Submesh submesh;
  submesh.BoundsMin = {-1.0F, -1.0F, -1.0F};
  submesh.BoundsMax = {1.0F, 1.0F, 1.0F};
  resources.Register("camera-fit", {submesh});

  const auto createMesh = [&scene](const char* name, DirectX::XMFLOAT3 position) {
    (void)scene.QueueCreate(name, Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::Mesh);
    const auto created = scene.sync();
    if (created.empty() || !created.front().Outcome) {
      return Engine::InvalidEntityHandle;
    }
    Engine::Ecs::MeshRenderer mesh;
    mesh.ModelGuid = "camera-fit";
    (void)scene.QueueSet(created.front().Entity, mesh);
    Engine::Ecs::LocalTransform transform;
    transform.Position = {position.x, position.y, position.z};
    (void)scene.QueueSet(created.front().Entity, transform);
    (void)scene.sync();
    return created.front().Entity;
  };
  ASSERT_NE(createMesh("Camera receiver", {0.0F, 0.0F, 0.0F}), Engine::InvalidEntityHandle);

  Engine::Viewport viewport(128, 128, nullptr);
  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -10.0F};
  camera.NearPlane = 0.1F;
  camera.FarPlane = 100.0F;
  Engine::Renderer::RenderView cameraView;
  cameraView.Initialize(&scene, camera, &viewport);

  Engine::Renderer::RenderView shadowView;
  shadowView.ViewType = Engine::Renderer::RenderView::Type::Shadow;
  std::vector<Engine::Renderer::RenderView*> shadowViews{&shadowView};
  std::vector<Engine::Renderer::RenderView*> sceneViews{&cameraView};
  Engine::SceneRenderer::BuildShadowData(&scene, shadowViews, 0, 1, &resources, sceneViews, 1024);
  const DirectX::XMMATRIX cameraRegionProjection = shadowView.ViewProjection;

  ASSERT_NE(createMesh("Distant offscreen mesh", {500.0F, 0.0F, 0.0F}), Engine::InvalidEntityHandle);
  Engine::SceneRenderer::BuildShadowData(&scene, shadowViews, 0, 1, &resources, sceneViews, 1024);
  EXPECT_TRUE(DirectX::XMVector4EqualInt(cameraRegionProjection.r[0], shadowView.ViewProjection.r[0]) &&
              DirectX::XMVector4EqualInt(cameraRegionProjection.r[1], shadowView.ViewProjection.r[1]) &&
              DirectX::XMVector4EqualInt(cameraRegionProjection.r[2], shadowView.ViewProjection.r[2]) &&
              DirectX::XMVector4EqualInt(cameraRegionProjection.r[3], shadowView.ViewProjection.r[3]));
}

TEST(SceneRendererShadowTests, DisabledLightsDoNotReceiveShadowSlots) {
  Engine::Scene scene;
  (void)AddLight(scene, Engine::Ecs::LightType::Point, false);
  (void)AddLight(scene, Engine::Ecs::LightType::Spot);

  ASSERT_EQ(Engine::SceneRenderer::CountShadowViews(&scene, 4), 1U);
  std::vector<std::unique_ptr<Engine::Renderer::RenderView>> ownedViews;
  ownedViews.push_back(std::make_unique<Engine::Renderer::RenderView>());
  std::vector<Engine::Renderer::RenderView*> views{ownedViews.front().get()};
  Engine::SceneRenderer::BuildShadowData(&scene, views, 0, 4);
  std::vector<Engine::Renderer::DirectionalLightData> lights;
  Engine::SceneRenderer::BuildLighting(&scene, views, lights);
  ASSERT_EQ(lights.size(), 2U);
  EXPECT_FLOAT_EQ(lights[0].ShadowParams.x, -1.0F);
  EXPECT_FLOAT_EQ(lights[1].ShadowParams.x, 0.0F);
}
