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

TEST(SceneRendererShadowTests, DirectionalProjectionTracksVisibleSceneGeometryBounds) {
  Engine::Scene scene;
  (void)AddLight(scene, Engine::Ecs::LightType::Directional);
  Engine::Renderer::ModelResourceLibrary resources;
  Engine::Renderer::Submesh submesh;
  submesh.BoundsMin = {-12.0F, -8.0F, -5.0F};
  submesh.BoundsMax = {12.0F, 8.0F, 5.0F};
  resources.Register("bounds-test", {submesh});

  std::vector<std::unique_ptr<Engine::Renderer::RenderView>> ownedViews;
  ownedViews.push_back(std::make_unique<Engine::Renderer::RenderView>());
  std::vector<Engine::Renderer::RenderView*> views{ownedViews.front().get()};
  Engine::SceneRenderer::BuildShadowData(&scene, views, 0, 1, &resources);
  const DirectX::XMMATRIX emptySceneProjection = views[0]->ViewProjection;

  (void)scene.QueueCreate("Mesh", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::Mesh);
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 1U);
  ASSERT_TRUE(created.front().Outcome);

  Engine::Ecs::MeshRenderer mesh;
  mesh.ModelGuid = "bounds-test";
  (void)scene.QueueSet(created.front().Entity, mesh);

  Engine::Ecs::LocalTransform transform;
  transform.Position = {500.0F, 0.0F, 0.0F};
  (void)scene.QueueSet(created.front().Entity, transform);
  (void)scene.sync();

  Engine::SceneRenderer::BuildShadowData(&scene, views, 0, 1, &resources);

  EXPECT_FALSE(DirectX::XMVector4EqualInt(emptySceneProjection.r[0], views[0]->ViewProjection.r[0]) &&
               DirectX::XMVector4EqualInt(emptySceneProjection.r[1], views[0]->ViewProjection.r[1]) &&
               DirectX::XMVector4EqualInt(emptySceneProjection.r[2], views[0]->ViewProjection.r[2]) &&
               DirectX::XMVector4EqualInt(emptySceneProjection.r[3], views[0]->ViewProjection.r[3]));
  EXPECT_NE(views[0]->ShadowLight, Engine::InvalidEntityHandle);

  const DirectX::XMMATRIX meshWorld =
      DirectX::XMLoadFloat4x4(&submesh.LocalTransform) *
      DirectX::XMLoadFloat4x4(&scene.World().Get<Engine::Ecs::WorldTransform>(created.front().Entity)->Matrix);
  for (uint32_t corner = 0; corner < 8; ++corner) {
    const DirectX::XMFLOAT3 localPoint{(corner & 1U) != 0U ? submesh.BoundsMax.x : submesh.BoundsMin.x,
                                       (corner & 2U) != 0U ? submesh.BoundsMax.y : submesh.BoundsMin.y,
                                       (corner & 4U) != 0U ? submesh.BoundsMax.z : submesh.BoundsMin.z};
    const DirectX::XMVECTOR clip = DirectX::XMVector3TransformCoord(
        DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&localPoint), meshWorld), views[0]->ViewProjection);
    EXPECT_GE(DirectX::XMVectorGetX(clip), -1.0F);
    EXPECT_LE(DirectX::XMVectorGetX(clip), 1.0F);
    EXPECT_GE(DirectX::XMVectorGetY(clip), -1.0F);
    EXPECT_LE(DirectX::XMVectorGetY(clip), 1.0F);
    EXPECT_GE(DirectX::XMVectorGetZ(clip), 0.0F);
    EXPECT_LE(DirectX::XMVectorGetZ(clip), 1.0F);
  }
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
