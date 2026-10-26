#include "runtime/include/Application.h"
#include "scene/include/Scene.h"
#include "scene/include/SceneDocument.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

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
                                                          .Scale = {2.0F, 3.0F, 4.0F},
                                                          .FlipHorizontal = true});
  (void)source.QueueSet(
      mesh, Engine::Ecs::MeshRenderer{.ModelGuid = "",
                                      .PrimitiveType = "cube",
                                      .CastShadows = false,
                                      .ReceiveShadows = false,
                                      .Visible = false,
                                      .Wireframe = true,
                                      .DisableDepthTest = true,
                                      .Material = Engine::SceneMaterialDocument{.baseColor = {0.2F, 0.4F, 0.6F, 1.0F},
                                                                                .metallic = 0.7F,
                                                                                .roughness = 0.25F,
                                                                                .brdfModel = "ggx",
                                                                                .alphaMode = "mask",
                                                                                .alphaCutoff = 0.35F,
                                                                                .doubleSided = true}});
  (void)source.QueueSet(mesh, Engine::Ecs::LocalVisibility{.IsVisible = false});
  (void)source.QueueSet(light, Engine::Ecs::LocalTransform{.Position = {1.0F, 2.0F, 3.0F},
                                                           .Rotation = {0.0F, 0.70710677F, 0.0F, 0.70710677F},
                                                           .Scale = {1.5F, 1.5F, 1.5F},
                                                           .FlipVertical = true});
  (void)source.QueueSet(light, Engine::Ecs::Name{.Value = "Key Light"});
  (void)source.QueueSet(light, Engine::Ecs::SpotLight{.Type = Engine::Ecs::LightType::Spot,
                                                      .Direction = {0.0F, -0.5F, 1.0F},
                                                      .Color = {0.25F, 0.5F, 0.75F},
                                                      .Intensity = 3.0F,
                                                      .Range = 42.0F,
                                                      .InnerAngleDegrees = 12.0F,
                                                      .OuterAngleDegrees = 32.0F,
                                                      .ShadowBias = 0.002F,
                                                      .ShadowNormalBias = 0.003F,
                                                      .CastShadows = false});
  (void)source.QueueSet(camera, Engine::Ecs::LocalTransform{.Position = {-4.0F, 5.0F, 6.0F},
                                                            .Rotation = {0.1F, 0.2F, 0.3F, 0.9F},
                                                            .Scale = {0.8F, 0.9F, 1.1F},
                                                            .FlipHorizontal = true,
                                                            .FlipVertical = true});
  (void)source.QueueSet(camera, Engine::Ecs::Camera{.FieldOfViewDegrees = 72.0F,
                                                    .NearPlane = 0.25F,
                                                    .FarPlane = 300.0F,
                                                    .OutputWidth = 2560,
                                                    .OutputHeight = 1440,
                                                    .Priority = -7,
                                                    .Active = false});
  (void)source.QueueAdd(
      camera,
      Engine::Ecs::CameraPostProcessing{
          .Effects = {Engine::Ecs::BloomEffect{.Threshold = 1.5F, .Intensity = 0.8F, .Radius = 6.0F}},
          .AmbientOcclusion = Engine::Ecs::ScreenSpaceAmbientOcclusionSettings{.Radius = 0.75F, .Intensity = 1.5F}});
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
  ASSERT_TRUE(saved.cameras.front().postProcessing.has_value());
  ASSERT_TRUE(saved.cameras.front().postProcessing->AmbientOcclusion.has_value());
  EXPECT_FLOAT_EQ(saved.cameras.front().postProcessing->AmbientOcclusion->Radius, 0.75F);
  ASSERT_EQ(saved.cameras.front().postProcessing->Effects.size(), 1U);

  const auto scenePath = std::filesystem::temp_directory_path() /
                         ("GraphicEngineMaterialRoundTrip-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
  std::string error;
  ASSERT_TRUE(Engine::SceneSerializer::SaveAtomic(scenePath, saved, error)) << error;
  const auto serialized = Engine::SceneSerializer::Load(scenePath);
  std::error_code filesystemError;
  std::filesystem::remove(scenePath, filesystemError);
  ASSERT_TRUE(serialized) << serialized.error;
  const auto serializedMesh =
      std::ranges::find(serialized.document.entities, std::string("Mesh Entity"), &Engine::SceneEntityDocument::name);
  ASSERT_NE(serializedMesh, serialized.document.entities.end());
  ASSERT_TRUE(serializedMesh->material.has_value());
  EXPECT_EQ(serializedMesh->material->baseColor, (std::array<float, 4>{0.2F, 0.4F, 0.6F, 1.0F}));
  EXPECT_EQ(serializedMesh->material->alphaMode, "mask");
  EXPECT_EQ(serialized.document.lights.front().type, "spot");

  std::vector<std::string> warnings;
  ASSERT_TRUE(application.LoadSceneDocument(serialized.document, error, warnings)) << error;
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
    EXPECT_EQ(actual->hasMeshRenderer, expected.hasMeshRenderer);
    EXPECT_EQ(actual->position, expected.position);
    EXPECT_EQ(actual->rotation, expected.rotation);
    EXPECT_EQ(actual->scale, expected.scale);
    EXPECT_EQ(actual->flipHorizontal, expected.flipHorizontal);
    EXPECT_EQ(actual->flipVertical, expected.flipVertical);
    EXPECT_EQ(actual->meshVisible, expected.meshVisible);
    EXPECT_EQ(actual->forceWireframe, expected.forceWireframe);
    EXPECT_EQ(actual->disableDepthTest, expected.disableDepthTest);
    EXPECT_EQ(actual->castShadows, expected.castShadows);
    EXPECT_EQ(actual->receiveShadows, expected.receiveShadows);
    EXPECT_EQ(actual->material, expected.material);
    EXPECT_EQ(actual->materialOverrides, expected.materialOverrides);
  }
  EXPECT_EQ(loaded.lights.front().name, saved.lights.front().name);
  EXPECT_EQ(loaded.lights.front().identityGuid, saved.lights.front().identityGuid);
  EXPECT_EQ(loaded.lights.front().parentGuid, saved.lights.front().parentGuid);
  EXPECT_EQ(loaded.lights.front().position, saved.lights.front().position);
  EXPECT_EQ(loaded.lights.front().rotation, saved.lights.front().rotation);
  EXPECT_EQ(loaded.lights.front().scale, saved.lights.front().scale);
  EXPECT_EQ(loaded.lights.front().flipHorizontal, saved.lights.front().flipHorizontal);
  EXPECT_EQ(loaded.lights.front().flipVertical, saved.lights.front().flipVertical);
  EXPECT_EQ(loaded.lights.front().direction, saved.lights.front().direction);
  EXPECT_EQ(loaded.lights.front().color, saved.lights.front().color);
  EXPECT_FLOAT_EQ(loaded.lights.front().intensity, saved.lights.front().intensity);
  EXPECT_FLOAT_EQ(loaded.lights.front().range, saved.lights.front().range);
  EXPECT_FLOAT_EQ(loaded.lights.front().innerAngleDegrees, saved.lights.front().innerAngleDegrees);
  EXPECT_FLOAT_EQ(loaded.lights.front().outerAngleDegrees, saved.lights.front().outerAngleDegrees);
  EXPECT_FLOAT_EQ(loaded.lights.front().shadowBias, saved.lights.front().shadowBias);
  EXPECT_FLOAT_EQ(loaded.lights.front().shadowNormalBias, saved.lights.front().shadowNormalBias);
  EXPECT_EQ(loaded.lights.front().usePosition, saved.lights.front().usePosition);
  EXPECT_EQ(loaded.lights.front().type, "spot");
  EXPECT_EQ(loaded.lights.front().castShadows, saved.lights.front().castShadows);
  EXPECT_EQ(loaded.cameras.front().name, saved.cameras.front().name);
  EXPECT_EQ(loaded.cameras.front().identityGuid, saved.cameras.front().identityGuid);
  EXPECT_EQ(loaded.cameras.front().parentGuid, saved.cameras.front().parentGuid);
  EXPECT_EQ(loaded.cameras.front().position, saved.cameras.front().position);
  EXPECT_EQ(loaded.cameras.front().rotation, saved.cameras.front().rotation);
  EXPECT_EQ(loaded.cameras.front().scale, saved.cameras.front().scale);
  ASSERT_TRUE(loaded.cameras.front().postProcessing.has_value());
  ASSERT_TRUE(loaded.cameras.front().postProcessing->AmbientOcclusion.has_value());
  EXPECT_FLOAT_EQ(loaded.cameras.front().postProcessing->AmbientOcclusion->Intensity, 1.5F);
  ASSERT_EQ(loaded.cameras.front().postProcessing->Effects.size(), 1U);
  const auto* restoredBloom = std::get_if<Engine::Ecs::BloomEffect>(&loaded.cameras.front().postProcessing->Effects[0]);
  ASSERT_NE(restoredBloom, nullptr);
  EXPECT_FLOAT_EQ(restoredBloom->Intensity, 0.8F);
  EXPECT_EQ(loaded.cameras.front().flipHorizontal, saved.cameras.front().flipHorizontal);
  EXPECT_EQ(loaded.cameras.front().flipVertical, saved.cameras.front().flipVertical);
  EXPECT_EQ(loaded.cameras.front().priority, saved.cameras.front().priority);
  EXPECT_EQ(loaded.cameras.front().active, saved.cameras.front().active);
  EXPECT_FLOAT_EQ(loaded.cameras.front().fovRadians, saved.cameras.front().fovRadians);
  EXPECT_FLOAT_EQ(loaded.cameras.front().nearPlane, saved.cameras.front().nearPlane);
  EXPECT_FLOAT_EQ(loaded.cameras.front().farPlane, saved.cameras.front().farPlane);
  EXPECT_EQ(loaded.cameras.front().outputWidth, saved.cameras.front().outputWidth);
  EXPECT_EQ(loaded.cameras.front().outputHeight, saved.cameras.front().outputHeight);
}

TEST(SceneEcsPersistenceTests, MissingMaterialReferenceKeepsSceneUsableAndPreservesLegacyValues) {
  Engine::Application application;
  Engine::SceneDocument document;
  Engine::SceneEntityDocument mesh;
  mesh.id = 7;
  mesh.name = "Missing material mesh";
  mesh.hasMeshRenderer = true;
  mesh.materialGuid = "00000000-0000-4000-8000-000000000099";
  Engine::SceneMaterialDocument legacy;
  legacy.baseColor = {0.3F, 0.5F, 0.7F, 1.0F};
  mesh.material = legacy;
  document.entities.push_back(mesh);

  std::string error;
  std::vector<std::string> warnings;
  ASSERT_TRUE(application.LoadSceneDocument(document, error, warnings)) << error;
  ASSERT_FALSE(warnings.empty());
  EXPECT_NE(warnings.front().find(*mesh.materialGuid), std::string::npos);
  ASSERT_EQ(application.GetScenes().size(), 1U);
  const Engine::SceneDocument saved = application.CreateSceneDocument(*application.GetScenes().front());
  ASSERT_EQ(saved.entities.size(), 2U);
  const auto restored = std::ranges::find(saved.entities, mesh.name, &Engine::SceneEntityDocument::name);
  ASSERT_NE(restored, saved.entities.end());
  EXPECT_EQ(restored->materialGuid, mesh.materialGuid);
  ASSERT_TRUE(restored->material.has_value());
  EXPECT_EQ(restored->material->baseColor, legacy.baseColor);
}

TEST(SceneEcsPersistenceTests, PersistsEmptyMeshRendererComponents) {
  Engine::Scene source;
  const uint64_t sequence = source.QueueCreate("Empty Mesh");
  const auto created = source.sync();
  ASSERT_EQ(created.size(), 1U);
  ASSERT_TRUE(created.front().Outcome);
  ASSERT_EQ(created.front().Sequence, sequence);
  (void)source.QueueAdd(created.front().Entity, Engine::Ecs::MeshRenderer{});
  const auto updated = source.sync();
  ASSERT_EQ(updated.size(), 1U);
  ASSERT_TRUE(updated.front().Outcome);

  Engine::Application application;
  const Engine::SceneDocument saved = application.CreateSceneDocument(source);
  const auto savedEntity =
      std::ranges::find(saved.entities, std::string("Empty Mesh"), &Engine::SceneEntityDocument::name);
  ASSERT_NE(savedEntity, saved.entities.end());
  EXPECT_TRUE(savedEntity->hasMeshRenderer);
  EXPECT_FALSE(savedEntity->modelGuid.has_value());
  EXPECT_FALSE(savedEntity->primitiveType.has_value());

  const auto path = std::filesystem::temp_directory_path() /
                    ("GraphicEngineEmptyMeshRenderer-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".scene.json");
  std::string error;
  ASSERT_TRUE(Engine::SceneSerializer::SaveAtomic(path, saved, error)) << error;
  const auto serialized = Engine::SceneSerializer::Load(path);
  std::error_code filesystemError;
  std::filesystem::remove(path, filesystemError);
  ASSERT_TRUE(serialized) << serialized.error;

  std::vector<std::string> warnings;
  ASSERT_TRUE(application.LoadSceneDocument(serialized.document, error, warnings)) << error;
  ASSERT_EQ(application.GetScenes().size(), 1U);
  const Engine::SceneDocument restored = application.CreateSceneDocument(*application.GetScenes().front());
  const auto restoredEntity =
      std::ranges::find(restored.entities, std::string("Empty Mesh"), &Engine::SceneEntityDocument::name);
  ASSERT_NE(restoredEntity, restored.entities.end());
  EXPECT_TRUE(restoredEntity->hasMeshRenderer);
}

TEST(SceneEcsPersistenceTests, LightTypeSwitchingValidatesAndPersistsDirectionalPointAndSpot) {
  Engine::Scene source;
  (void)source.QueueCreate("Editable Light", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::SpotLight);
  const auto created = source.sync();
  ASSERT_EQ(created.size(), 1U);
  ASSERT_TRUE(created.front().Outcome);
  const Engine::EntityHandle lightEntity = created.front().Entity;
  Engine::Ecs::Light light;
  light.Type = Engine::Ecs::LightType::Directional;
  light.Range = -2.0F;
  light.InnerAngleDegrees = 20.0F;
  light.OuterAngleDegrees = 5.0F;
  EXPECT_TRUE(Engine::Ecs::Validate(light));
  light.Type = Engine::Ecs::LightType::Point;
  light.Range = 25.0F;
  light.Direction = {0.0F, 0.0F, 0.0F};
  light.InnerAngleDegrees = 20.0F;
  light.OuterAngleDegrees = 5.0F;
  EXPECT_TRUE(Engine::Ecs::Validate(light));
  light.Type = Engine::Ecs::LightType::Spot;
  light.Direction = {0.0F, -1.0F, 0.0F};
  light.InnerAngleDegrees = 15.0F;
  light.OuterAngleDegrees = 35.0F;
  EXPECT_TRUE(Engine::Ecs::Validate(light));
  light.OuterAngleDegrees = light.InnerAngleDegrees;
  EXPECT_FALSE(Engine::Ecs::Validate(light));
  light.OuterAngleDegrees = 35.0F;
  (void)source.QueueSet(lightEntity, light);
  ASSERT_TRUE(source.sync().front().Outcome);

  Engine::Application application;
  const auto saved = application.CreateSceneDocument(source);
  ASSERT_EQ(saved.lights.size(), 1U);
  EXPECT_EQ(saved.lights.front().type, "spot");
  std::string error;
  std::vector<std::string> warnings;
  ASSERT_TRUE(application.LoadSceneDocument(saved, error, warnings)) << error;
  const auto loaded = application.CreateSceneDocument(*application.GetScenes().front());
  ASSERT_EQ(loaded.lights.size(), 1U);
  EXPECT_EQ(loaded.lights.front().type, "spot");
  EXPECT_FLOAT_EQ(loaded.lights.front().innerAngleDegrees, 15.0F);
  EXPECT_FLOAT_EQ(loaded.lights.front().outerAngleDegrees, 35.0F);
}

TEST(SceneEcsPersistenceTests, SceneJsonRoundTripsEachLightTypeAndTypeSpecificValidation) {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory = std::filesystem::temp_directory_path() / ("GraphicEngineLightTypes-" + std::to_string(nonce));
  std::error_code filesystemError;
  ASSERT_TRUE(std::filesystem::create_directories(directory, filesystemError));
  const auto cleanup = [&directory]() {
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  };
  const std::array<std::pair<Engine::Ecs::LightType, std::string>, 3> types = {
      {{Engine::Ecs::LightType::Directional, "directional"},
       {Engine::Ecs::LightType::Point, "point"},
       {Engine::Ecs::LightType::Spot, "spot"}}};
  for (const auto& [type, expectedName] : types) {
    Engine::SceneDocument document;
    Engine::SceneLightDocument light;
    light.id = 1;
    light.name = "Test Light";
    light.type = expectedName;
    if (type == Engine::Ecs::LightType::Directional) {
      light.range = -1.0F;
      light.innerAngleDegrees = 25.0F;
      light.outerAngleDegrees = 10.0F;
    } else if (type == Engine::Ecs::LightType::Point) {
      light.direction = {0.0F, 0.0F, 0.0F};
      light.innerAngleDegrees = 25.0F;
      light.outerAngleDegrees = 10.0F;
    }
    document.lights.push_back(light);
    std::string error;
    const auto path = directory / (expectedName + ".json");
    ASSERT_TRUE(Engine::SceneSerializer::SaveAtomic(path, document, error)) << error;
    const auto loaded = Engine::SceneSerializer::Load(path);
    ASSERT_TRUE(loaded) << loaded.error;
    ASSERT_EQ(loaded.document.lights.size(), 1U);
    EXPECT_EQ(loaded.document.lights.front().type, expectedName);
  }
  cleanup();
}

TEST(SceneEcsPersistenceTests, UnsupportedLightTypeDoesNotReplaceActiveScene) {
  Engine::Application application;
  Engine::SceneDocument initial;
  std::string error;
  std::vector<std::string> warnings;
  ASSERT_TRUE(application.LoadSceneDocument(initial, error, warnings)) << error;
  const auto activeBefore = application.GetScenes().front();

  Engine::SceneDocument invalid;
  Engine::SceneLightDocument light;
  light.id = 9;
  light.name = "Invalid Light";
  light.type = "area";
  invalid.lights.push_back(light);
  EXPECT_FALSE(application.LoadSceneDocument(invalid, error, warnings));
  EXPECT_NE(error.find("unsupported light type"), std::string::npos) << error;
  ASSERT_EQ(application.GetScenes().size(), 1U);
  EXPECT_EQ(application.GetScenes().front(), activeBefore);
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
  const Engine::SceneDocument stateBefore = application.CreateSceneDocument(*activeBefore);
  ASSERT_EQ(stateBefore.entities.size(), 2U);
  const auto existingBefore =
      std::ranges::find(stateBefore.entities, std::string("Existing"), &Engine::SceneEntityDocument::name);
  ASSERT_NE(existingBefore, stateBefore.entities.end());

  Engine::SceneDocument invalid;
  invalid.entities.push_back(entity);
  invalid.entities.back().name = "Rejected";
  invalid.entities.back().primitiveType = "unsupported";
  EXPECT_FALSE(application.LoadSceneDocument(invalid, error, warnings));
  EXPECT_NE(error.find("unsupported primitive type"), std::string::npos) << error;
  ASSERT_EQ(application.GetScenes().size(), 1U);
  EXPECT_EQ(application.GetScenes().front(), activeBefore);
  const Engine::SceneDocument stillActive = application.CreateSceneDocument(*activeBefore);
  ASSERT_EQ(stillActive.entities.size(), stateBefore.entities.size());
  const auto existingAfter =
      std::ranges::find(stillActive.entities, std::string("Existing"), &Engine::SceneEntityDocument::name);
  ASSERT_NE(existingAfter, stillActive.entities.end());
  EXPECT_EQ(existingAfter->identityGuid, existingBefore->identityGuid);
  const auto rootBefore = std::ranges::find(stateBefore.entities, true, &Engine::SceneEntityDocument::sceneRoot);
  const auto rootAfter = std::ranges::find(stillActive.entities, true, &Engine::SceneEntityDocument::sceneRoot);
  ASSERT_NE(rootBefore, stateBefore.entities.end());
  ASSERT_NE(rootAfter, stillActive.entities.end());
  EXPECT_EQ(rootAfter->identityGuid, rootBefore->identityGuid);
}

} // namespace
