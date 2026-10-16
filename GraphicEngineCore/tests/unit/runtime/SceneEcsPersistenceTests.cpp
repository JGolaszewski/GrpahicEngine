#include "runtime/include/Application.h"
#include "scene/include/Scene.h"
#include "scene/include/SceneDocument.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

TEST(SceneEcsPersistenceTests, RoundTripsEntityIdentityHierarchyAndSupportedComponents) {
  Engine::Scene source;
  const uint64_t meshSequence =
      source.QueueCreate("Mesh Entity", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::Mesh);
  const uint64_t lightSequence =
      source.QueueCreate("Key Light", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::SpotLight);
  const uint64_t cameraSequence =
      source.QueueCreate("Main Camera", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::Camera);
  const auto created = source.sync();
  ASSERT_EQ(created.size(), 3U);
  Engine::EntityHandle mesh = Engine::InvalidEntityHandle;
  Engine::EntityHandle light = Engine::InvalidEntityHandle;
  Engine::EntityHandle camera = Engine::InvalidEntityHandle;
  for (const auto& result : created) {
    ASSERT_TRUE(result.Outcome);
    if (result.Sequence == meshSequence)
      mesh = result.Entity;
    if (result.Sequence == lightSequence)
      light = result.Entity;
    if (result.Sequence == cameraSequence)
      camera = result.Entity;
  }
  ASSERT_NE(mesh, Engine::InvalidEntityHandle);
  ASSERT_NE(light, Engine::InvalidEntityHandle);
  ASSERT_NE(camera, Engine::InvalidEntityHandle);

  (void)source.QueueReparent(light, mesh);
  (void)source.QueueReparent(camera, light);
  (void)source.QueueSet(mesh, Engine::Ecs::LocalTransform{.Position = {2.0F, -3.0F, 4.0F},
                                                          .Rotation = {0.0F, 0.0F, 0.38268343F, 0.9238795F},
                                                          .Scale = {2.0F, 3.0F, 4.0F}});
  (void)source.QueueSet(mesh, Engine::Ecs::MeshRenderer{.ModelGuid = "",
                                                        .PrimitiveType = "cube",
                                                        .CastShadows = false,
                                                        .ReceiveShadows = false,
                                                        .Visible = false,
                                                        .Wireframe = true,
                                                        .DisableDepthTest = true});
  (void)source.QueueSet(mesh, Engine::Ecs::LocalVisibility{.IsVisible = false});
  (void)source.QueueSet(light, Engine::Ecs::LocalTransform{.Position = {1.0F, 2.0F, 3.0F},
                                                           .Rotation = {0.0F, 0.70710677F, 0.0F, 0.70710677F},
                                                           .Scale = {1.5F, 1.5F, 1.5F}});
  (void)source.QueueSet(light, Engine::Ecs::Name{.Value = "Key Light"});
  (void)source.QueueSet(light, Engine::Ecs::SpotLight{.Direction = {0.0F, -0.5F, 1.0F},
                                                      .Color = {0.25F, 0.5F, 0.75F},
                                                      .Intensity = 3.0F,
                                                      .Range = 42.0F,
                                                      .InnerAngleDegrees = 12.0F,
                                                      .OuterAngleDegrees = 32.0F,
                                                      .ShadowBias = 0.002F,
                                                      .ShadowNormalBias = 0.003F,
                                                      .UsePosition = false,
                                                      .CastShadows = false});
  (void)source.QueueSet(camera, Engine::Ecs::LocalTransform{.Position = {-4.0F, 5.0F, 6.0F},
                                                            .Rotation = {0.1F, 0.2F, 0.3F, 0.9F},
                                                            .Scale = {0.8F, 0.9F, 1.1F}});
  (void)source.QueueSet(camera, Engine::Ecs::Camera{.FieldOfViewDegrees = 72.0F,
                                                    .NearPlane = 0.25F,
                                                    .FarPlane = 300.0F,
                                                    .OutputWidth = 2560,
                                                    .OutputHeight = 1440,
                                                    .Priority = -7,
                                                    .Active = false});
  for (const auto& result : source.sync())
    ASSERT_TRUE(result.Outcome);

  Engine::Application application;
  const Engine::SceneDocument saved = application.CreateSceneDocument(source);
  ASSERT_EQ(saved.entities.size(), 4U);
  ASSERT_EQ(saved.lights.size(), 1U);
  ASSERT_EQ(saved.cameras.size(), 1U);
  EXPECT_EQ(saved.lights.front().name, "Key Light");
  EXPECT_EQ(saved.cameras.front().name, "Main Camera");
  EXPECT_EQ(saved.cameras.front().priority, -7);
  EXPECT_FALSE(saved.cameras.front().active);

  std::string error;
  std::vector<std::string> warnings;
  ASSERT_TRUE(application.LoadSceneDocument(saved, error, warnings)) << error;
  ASSERT_EQ(application.GetScenes().size(), 1U);
  const Engine::SceneDocument loaded = application.CreateSceneDocument(*application.GetScenes().front());
  ASSERT_EQ(loaded.entities.size(), saved.entities.size());
  ASSERT_EQ(loaded.lights.size(), 1U);
  ASSERT_EQ(loaded.cameras.size(), 1U);
  for (const auto& expected : saved.entities) {
    const auto actual =
        std::ranges::find(loaded.entities, expected.identityGuid, &Engine::SceneEntityDocument::identityGuid);
    ASSERT_NE(actual, loaded.entities.end());
    EXPECT_EQ(actual->id, expected.id);
    EXPECT_EQ(actual->parentGuid, expected.parentGuid);
    EXPECT_EQ(actual->name, expected.name);
    EXPECT_EQ(actual->visible, expected.visible);
    EXPECT_EQ(actual->primitiveType, expected.primitiveType);
    EXPECT_EQ(actual->position, expected.position);
    EXPECT_EQ(actual->rotation, expected.rotation);
    EXPECT_EQ(actual->scale, expected.scale);
    EXPECT_EQ(actual->meshVisible, expected.meshVisible);
    EXPECT_EQ(actual->forceWireframe, expected.forceWireframe);
    EXPECT_EQ(actual->disableDepthTest, expected.disableDepthTest);
    EXPECT_EQ(actual->castShadows, expected.castShadows);
    EXPECT_EQ(actual->receiveShadows, expected.receiveShadows);
  }
  EXPECT_EQ(loaded.lights.front().name, saved.lights.front().name);
  EXPECT_EQ(loaded.lights.front().identityGuid, saved.lights.front().identityGuid);
  EXPECT_EQ(loaded.lights.front().parentGuid, saved.lights.front().parentGuid);
  EXPECT_EQ(loaded.lights.front().position, saved.lights.front().position);
  EXPECT_EQ(loaded.lights.front().rotation, saved.lights.front().rotation);
  EXPECT_EQ(loaded.lights.front().scale, saved.lights.front().scale);
  EXPECT_EQ(loaded.lights.front().direction, saved.lights.front().direction);
  EXPECT_EQ(loaded.lights.front().color, saved.lights.front().color);
  EXPECT_FLOAT_EQ(loaded.lights.front().intensity, saved.lights.front().intensity);
  EXPECT_FLOAT_EQ(loaded.lights.front().range, saved.lights.front().range);
  EXPECT_FLOAT_EQ(loaded.lights.front().innerAngleDegrees, saved.lights.front().innerAngleDegrees);
  EXPECT_FLOAT_EQ(loaded.lights.front().outerAngleDegrees, saved.lights.front().outerAngleDegrees);
  EXPECT_FLOAT_EQ(loaded.lights.front().shadowBias, saved.lights.front().shadowBias);
  EXPECT_FLOAT_EQ(loaded.lights.front().shadowNormalBias, saved.lights.front().shadowNormalBias);
  EXPECT_EQ(loaded.lights.front().usePosition, saved.lights.front().usePosition);
  EXPECT_EQ(loaded.lights.front().castShadows, saved.lights.front().castShadows);
  EXPECT_EQ(loaded.cameras.front().name, saved.cameras.front().name);
  EXPECT_EQ(loaded.cameras.front().identityGuid, saved.cameras.front().identityGuid);
  EXPECT_EQ(loaded.cameras.front().parentGuid, saved.cameras.front().parentGuid);
  EXPECT_EQ(loaded.cameras.front().position, saved.cameras.front().position);
  EXPECT_EQ(loaded.cameras.front().rotation, saved.cameras.front().rotation);
  EXPECT_EQ(loaded.cameras.front().scale, saved.cameras.front().scale);
  EXPECT_EQ(loaded.cameras.front().priority, saved.cameras.front().priority);
  EXPECT_EQ(loaded.cameras.front().active, saved.cameras.front().active);
  EXPECT_FLOAT_EQ(loaded.cameras.front().fovRadians, saved.cameras.front().fovRadians);
  EXPECT_FLOAT_EQ(loaded.cameras.front().nearPlane, saved.cameras.front().nearPlane);
  EXPECT_FLOAT_EQ(loaded.cameras.front().farPlane, saved.cameras.front().farPlane);
  EXPECT_EQ(loaded.cameras.front().outputWidth, saved.cameras.front().outputWidth);
  EXPECT_EQ(loaded.cameras.front().outputHeight, saved.cameras.front().outputHeight);
}

TEST(SceneEcsPersistenceTests, FailedInstantiationPreservesActiveScene) {
  Engine::Application application;
  Engine::SceneDocument initial;
  Engine::SceneEntityDocument entity;
  entity.id = 7;
  entity.name = "Existing";
  entity.primitiveType = "cube";
  initial.entities.push_back(entity);
  std::string error;
  std::vector<std::string> warnings;
  ASSERT_TRUE(application.LoadSceneDocument(initial, error, warnings)) << error;
  const auto activeBefore = application.GetScenes().front();

  Engine::SceneDocument invalid;
  invalid.entities.push_back(entity);
  invalid.entities.back().name = "Rejected";
  invalid.entities.back().primitiveType = "unsupported";
  EXPECT_FALSE(application.LoadSceneDocument(invalid, error, warnings));
  EXPECT_NE(error.find("unsupported primitive type"), std::string::npos) << error;
  ASSERT_EQ(application.GetScenes().size(), 1U);
  EXPECT_EQ(application.GetScenes().front(), activeBefore);
  const Engine::SceneDocument stillActive = application.CreateSceneDocument(*activeBefore);
  ASSERT_EQ(stillActive.entities.size(), 1U);
  EXPECT_EQ(stillActive.entities.front().name, "Existing");
}

} // namespace
