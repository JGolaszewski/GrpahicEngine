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
  EXPECT_NE(views[1]->ViewProjection.r[0].m128_f32[0], views[2]->ViewProjection.r[0].m128_f32[0]);

  std::vector<Engine::Renderer::DirectionalLightData> lights;
  Engine::SceneRenderer::BuildLighting(&scene, views, lights);
  ASSERT_EQ(lights.size(), 3U);
  EXPECT_FLOAT_EQ(lights[0].ShadowParams.x, 0.0F);
  EXPECT_FLOAT_EQ(lights[1].ShadowParams.x, 1.0F);
  EXPECT_FLOAT_EQ(lights[2].ShadowParams.x, 7.0F);
  EXPECT_FLOAT_EQ(lights[1].ConeParams.z, 1.0F);
  EXPECT_FLOAT_EQ(lights[0].ConeParams.z, 0.0F);
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
