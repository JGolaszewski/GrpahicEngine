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
