#include "rendering/include/renderer/CameraView.h"

#include <gtest/gtest.h>

#include <DirectXMath.h>

namespace {

TEST(CameraViewTests, ResolvesSceneCameraProjectionAndWorldPose) {
  Engine::Ecs::Camera camera;
  camera.FieldOfViewDegrees = 72.0F;
  camera.NearPlane = 0.25F;
  camera.FarPlane = 250.0F;
  Engine::Ecs::WorldTransform transform;
  DirectX::XMStoreFloat4x4(&transform.Matrix, DirectX::XMMatrixTranslation(3.0F, 4.0F, 5.0F));

  const auto view = Engine::Renderer::ResolveSceneCameraView(camera, transform);

  ASSERT_TRUE(view.has_value());
  EXPECT_FLOAT_EQ(view->Position[0], 3.0F);
  EXPECT_FLOAT_EQ(view->Position[1], 4.0F);
  EXPECT_FLOAT_EQ(view->Position[2], 5.0F);
  EXPECT_FLOAT_EQ(view->Rotation[0], 0.0F);
  EXPECT_FLOAT_EQ(view->Rotation[1], 0.0F);
  EXPECT_FLOAT_EQ(view->Rotation[2], 0.0F);
  EXPECT_FLOAT_EQ(view->Rotation[3], 1.0F);
  EXPECT_FLOAT_EQ(view->FieldOfViewDegrees, 72.0F);
  EXPECT_FLOAT_EQ(view->NearPlane, 0.25F);
  EXPECT_FLOAT_EQ(view->FarPlane, 250.0F);
}

TEST(CameraViewTests, RejectsUndecomposableWorldTransform) {
  Engine::Ecs::Camera camera;
  Engine::Ecs::WorldTransform transform;
  transform.Matrix = {};

  EXPECT_FALSE(Engine::Renderer::ResolveSceneCameraView(camera, transform).has_value());
}

} // namespace
