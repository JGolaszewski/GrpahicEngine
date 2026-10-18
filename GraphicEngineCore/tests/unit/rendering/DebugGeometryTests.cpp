#include "rendering/include/renderer/DebugGeometry.h"
#include "scene/include/Scene.h"

#include <gtest/gtest.h>

TEST(DebugGeometryTests, CameraFrustumUsesConfiguredResolutionAspectRatio) {
  Engine::Scene scene;
  const uint64_t createSequence =
      scene.QueueCreate("Camera", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::Camera);
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 1U);
  ASSERT_EQ(created.front().Sequence, createSequence);
  ASSERT_TRUE(created.front().Outcome);
  const Engine::EntityHandle cameraEntity = created.front().Entity;
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Renderer::DebugRenderSettings settings;
  settings.ShowCameraFrustums = true;
  Engine::Renderer::DebugGeometry geometry;
  Engine::Renderer::DebugGeometryBuilder::BuildScene(scene, settings, geometry);
  ASSERT_EQ(geometry.GetLineVertices().size(), 24U);
  const DirectX::XMFLOAT3 widescreenFarCorner = geometry.GetLineVertices().at(2).Position;

  Engine::Ecs::Camera camera = *scene.World().Get<Engine::Ecs::Camera>(cameraEntity);
  camera.OutputWidth = 4;
  camera.OutputHeight = 3;
  ASSERT_TRUE(scene.SetInPlace(cameraEntity, camera));
  Engine::Renderer::DebugGeometryBuilder::BuildScene(scene, settings, geometry);
  ASSERT_EQ(geometry.GetLineVertices().size(), 24U);
  const DirectX::XMFLOAT3 fourByThreeFarCorner = geometry.GetLineVertices().at(2).Position;
  EXPECT_NE(widescreenFarCorner.x, fourByThreeFarCorner.x);
  EXPECT_FLOAT_EQ(widescreenFarCorner.y, fourByThreeFarCorner.y);
}

TEST(DebugGeometryTests, CameraRejectsZeroOutputDimensions) {
  Engine::Ecs::Camera camera;
  camera.OutputWidth = 0;
  EXPECT_FALSE(Engine::Ecs::Validate(camera));
  camera.OutputWidth = 1920;
  camera.OutputHeight = 0;
  EXPECT_FALSE(Engine::Ecs::Validate(camera));
}

TEST(DebugGeometryTests, DrawsTypeSpecificLightShapesAndHonorsVisibilitySettings) {
  Engine::Scene scene;
  const uint64_t sequence =
      scene.QueueCreate("Light", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::SpotLight);
  const auto creationResults = scene.sync();
  ASSERT_EQ(creationResults.size(), 1U);
  ASSERT_EQ(creationResults.front().Sequence, sequence);
  ASSERT_TRUE(creationResults.front().Outcome);
  const Engine::EntityHandle lightEntity = creationResults.front().Entity;
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Ecs::Light light = *scene.World().Get<Engine::Ecs::Light>(lightEntity);
  Engine::Renderer::DebugRenderSettings settings;
  settings.ShowLightGizmos = true;
  Engine::Renderer::DebugGeometry geometry;

  light.Type = Engine::Ecs::LightType::Directional;
  ASSERT_TRUE(scene.SetInPlace(lightEntity, light));
  Engine::Ecs::LocalTransform localTransform = *scene.World().Get<Engine::Ecs::LocalTransform>(lightEntity);
  localTransform.Position = {2.0F, 3.0F, 4.0F};
  ASSERT_TRUE(scene.SetInPlace(lightEntity, localTransform));
  ASSERT_TRUE(scene.Update(0.0F));
  Engine::Renderer::DebugGeometryBuilder::BuildScene(scene, settings, geometry);
  EXPECT_EQ(geometry.GetLineVertices().size(), 10U);
  EXPECT_FLOAT_EQ(geometry.GetLineVertices().front().Position.x, 2.0F);
  EXPECT_FLOAT_EQ(geometry.GetLineVertices().front().Position.y, 3.0F);
  EXPECT_FLOAT_EQ(geometry.GetLineVertices().front().Position.z, 4.0F);
  ASSERT_EQ(geometry.GetLightTargets().size(), 1U);
  EXPECT_EQ(geometry.GetLightTargets().front().Type, Engine::Ecs::LightType::Directional);

  light.Type = Engine::Ecs::LightType::Point;
  light.Range = 4.0F;
  ASSERT_TRUE(scene.SetInPlace(lightEntity, light));
  Engine::Renderer::DebugGeometryBuilder::BuildScene(scene, settings, geometry);
  EXPECT_EQ(geometry.GetLineVertices().size(), 3U * 32U * 2U);
  const DirectX::XMFLOAT3 firstSpherePoint = geometry.GetLineVertices().front().Position;
  const float offsetX = firstSpherePoint.x - 2.0F;
  const float offsetY = firstSpherePoint.y - 3.0F;
  const float offsetZ = firstSpherePoint.z - 4.0F;
  EXPECT_FLOAT_EQ(std::sqrt(offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ), 4.0F);
  EXPECT_EQ(geometry.GetLightTargets().front().Type, Engine::Ecs::LightType::Point);

  light.Type = Engine::Ecs::LightType::Spot;
  light.Range = 6.0F;
  light.OuterAngleDegrees = 30.0F;
  ASSERT_TRUE(scene.SetInPlace(lightEntity, light));
  Engine::Renderer::DebugGeometryBuilder::BuildScene(scene, settings, geometry);
  EXPECT_EQ(geometry.GetLineVertices().size(), 134U);
  EXPECT_EQ(geometry.GetLightTargets().front().Type, Engine::Ecs::LightType::Spot);

  settings.ShowLightGizmos = false;
  Engine::Renderer::DebugGeometryBuilder::BuildScene(scene, settings, geometry);
  EXPECT_TRUE(geometry.GetLineVertices().empty());
  EXPECT_TRUE(geometry.GetLightTargets().empty());

  settings.ShowLightGizmos = true;
  ASSERT_TRUE(scene.SetInPlace(lightEntity, Engine::Ecs::LocalVisibility{.IsVisible = false}));
  ASSERT_TRUE(scene.Update(0.0F));
  Engine::Renderer::DebugGeometryBuilder::BuildScene(scene, settings, geometry);
  EXPECT_TRUE(geometry.GetLineVertices().empty());
  EXPECT_TRUE(geometry.GetLightTargets().empty());
}
