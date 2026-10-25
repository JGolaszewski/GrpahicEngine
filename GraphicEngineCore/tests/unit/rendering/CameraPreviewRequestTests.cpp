#include "panels/include/CameraPreviewRequest.h"

#include <gtest/gtest.h>

TEST(CameraPreviewRequestTests, ResolvesSelectedEntityAndConfiguredOutputDimensions) {
  Engine::ArchetypeWorld world;
  Engine::Ecs::Camera firstCamera;
  firstCamera.OutputWidth = 1600;
  firstCamera.OutputHeight = 900;
  Engine::Ecs::Camera selectedCamera;
  selectedCamera.FieldOfViewDegrees = 72.0F;
  selectedCamera.OutputWidth = 800;
  selectedCamera.OutputHeight = 600;
  Engine::Ecs::WorldTransform transform;
  Engine::Ecs::CameraPostProcessing selectedPostProcessing{
      .Effects = {Engine::Ecs::ColorGradingEffect{.Exposure = 1.0F}}};

  const auto firstEntity = world.Create(firstCamera, transform);
  const auto selectedEntity = world.Create(selectedCamera, transform, selectedPostProcessing);
  ASSERT_TRUE(firstEntity);
  ASSERT_TRUE(selectedEntity);

  const auto request = ResolveCameraPreviewRequest(world, *selectedEntity);

  ASSERT_TRUE(request);
  EXPECT_EQ(request->Width, 800U);
  EXPECT_EQ(request->Height, 600U);
  EXPECT_FLOAT_EQ(request->View.FieldOfViewDegrees, 72.0F);
  ASSERT_EQ(request->PostProcessing.Effects.size(), 1U);
  const auto* grading = std::get_if<Engine::Ecs::ColorGradingEffect>(&request->PostProcessing.Effects.front());
  ASSERT_NE(grading, nullptr);
  EXPECT_FLOAT_EQ(grading->Exposure, 1.0F);
}

TEST(CameraPreviewRequestTests, RefreshResolutionReadsCurrentSceneCameraSettings) {
  Engine::ArchetypeWorld world;
  Engine::Ecs::Camera camera;
  camera.OutputWidth = 800;
  camera.OutputHeight = 600;
  Engine::Ecs::WorldTransform transform;
  const auto entity = world.Create(camera, transform);
  ASSERT_TRUE(entity);

  const auto initial = ResolveCameraPreviewRequest(world, *entity);
  ASSERT_TRUE(initial);
  ASSERT_TRUE(world.Set(*entity, Engine::Ecs::Camera{.OutputWidth = 2048, .OutputHeight = 1536}));
  const auto refreshed = ResolveCameraPreviewRequest(world, *entity);

  ASSERT_TRUE(refreshed);
  EXPECT_EQ(refreshed->Width, 2048U);
  EXPECT_EQ(refreshed->Height, 1536U);
}

TEST(CameraPreviewRequestTests, RejectsAnEntityWithoutAValidCamera) {
  Engine::ArchetypeWorld world;
  const auto entity = world.Create(Engine::Ecs::Name{"Not a camera"});
  ASSERT_TRUE(entity);

  EXPECT_FALSE(ResolveCameraPreviewRequest(world, *entity));
}
