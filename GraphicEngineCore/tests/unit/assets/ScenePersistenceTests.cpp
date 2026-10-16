#include "assets/include/AssetManager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {

class TemporaryProject final {
public:
  TemporaryProject() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    m_Root = std::filesystem::temp_directory_path() / ("GraphicEngineScenePersistenceTests-" + std::to_string(nonce));
    std::filesystem::create_directories(m_Root);
  }

  TemporaryProject(const TemporaryProject&) = delete;
  auto operator=(const TemporaryProject&) -> TemporaryProject& = delete;
  TemporaryProject(TemporaryProject&&) = delete;
  auto operator=(TemporaryProject&&) -> TemporaryProject& = delete;

  ~TemporaryProject() noexcept {
    std::error_code ignored;
    try {
      std::filesystem::remove_all(m_Root, ignored);
    } catch (...) {
      std::fputs("Scene persistence test workspace cleanup failed\n", stderr);
    }
  }

  [[nodiscard]] auto Root() const noexcept -> const std::filesystem::path& { return m_Root; }

private:
  std::filesystem::path m_Root;
};

TEST(ScenePersistenceTests, RoundTripsFieldsAndRejectsTraversalWithoutOverwritingValidFile) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  Engine::SceneDocument document;
  document.unitsPerMeter = 0.25F;
  Engine::SceneEntityDocument entity;
  entity.id = 23;
  entity.name = "persisted entity";
  entity.primitiveType = "cube";
  entity.position = {1.0F, -2.0F, 3.5F};
  entity.forceWireframe = true;
  document.entities.push_back(entity);

  std::string error;
  ASSERT_TRUE(assets.SaveScene("level.scene.json", document, error)) << error;
  const auto originalBytes = [&project] {
    std::ifstream input(project.Root() / "level.scene.json", std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  }();

  EXPECT_FALSE(assets.SaveScene("../escaped.scene.json", document, error));
  EXPECT_EQ(originalBytes, [&project] {
    std::ifstream input(project.Root() / "level.scene.json", std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  }());

  const auto loaded = assets.LoadScene("level.scene.json");
  ASSERT_TRUE(loaded) << loaded.error;
  ASSERT_EQ(loaded.document.entities.size(), 1U);
  EXPECT_FLOAT_EQ(loaded.document.unitsPerMeter, 0.25F);
  EXPECT_EQ(loaded.document.entities.front().id, 23U);
  EXPECT_EQ(loaded.document.entities.front().name, "persisted entity");
  ASSERT_TRUE(loaded.document.entities.front().primitiveType.has_value());
  EXPECT_EQ(*loaded.document.entities.front().primitiveType, "cube");
  EXPECT_EQ(loaded.document.entities.front().position, (std::array<float, 3>{1.0F, -2.0F, 3.5F}));
  EXPECT_TRUE(loaded.document.entities.front().forceWireframe);
}

TEST(ScenePersistenceTests, SchemaThreeRoundTripsSceneRootAndEmptyHierarchyEntities) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  Engine::SceneDocument document;
  Engine::SceneEntityDocument root;
  root.id = 1;
  root.identityGuid = Engine::SceneGuidDocument{.high = 8, .low = 1};
  root.name = "Level One";
  root.sceneRoot = true;
  document.entities.push_back(root);
  Engine::SceneEntityDocument emptyEntity;
  emptyEntity.id = 2;
  emptyEntity.identityGuid = Engine::SceneGuidDocument{.high = 8, .low = 2};
  emptyEntity.parentGuid = root.identityGuid;
  emptyEntity.name = "Empty Entity";
  document.entities.push_back(emptyEntity);

  std::string error;
  ASSERT_TRUE(assets.SaveScene("scene-root.scene.json", document, error)) << error;
  const auto loaded = assets.LoadScene("scene-root.scene.json");
  ASSERT_TRUE(loaded) << loaded.error;
  ASSERT_EQ(loaded.document.entities.size(), 2U);
  EXPECT_TRUE(loaded.document.entities[0].sceneRoot);
  EXPECT_EQ(loaded.document.entities[0].name, "Level One");
  EXPECT_FALSE(loaded.document.entities[1].sceneRoot);
  EXPECT_EQ(loaded.document.entities[1].parentGuid, root.identityGuid);
}

TEST(ScenePersistenceTests, PersistsCameraResolutionAndUsesFullHdDefaultsForOlderScenes) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  Engine::SceneDocument document;
  Engine::SceneCameraDocument camera;
  camera.id = 7;
  camera.name = "Main View";
  camera.rotation = {0.0F, 0.70710677F, 0.0F, 0.70710677F};
  camera.scale = {1.2F, 1.3F, 1.4F};
  camera.outputWidth = 2560;
  camera.outputHeight = 1440;
  camera.priority = -4;
  camera.active = false;
  document.cameras.push_back(camera);
  Engine::SceneEntityDocument mesh;
  mesh.id = 9;
  mesh.name = "Hidden Mesh";
  mesh.primitiveType = "cube";
  mesh.meshVisible = false;
  document.entities.push_back(mesh);
  Engine::SceneLightDocument light;
  light.id = 8;
  light.name = "Fill Light";
  light.position = {3.0F, 4.0F, 5.0F};
  light.rotation = {0.0F, 0.0F, 0.38268343F, 0.9238795F};
  light.scale = {2.0F, 2.0F, 2.0F};
  document.lights.push_back(light);

  std::string error;
  ASSERT_TRUE(assets.SaveScene("camera.scene.json", document, error)) << error;
  const auto loaded = assets.LoadScene("camera.scene.json");
  ASSERT_TRUE(loaded) << loaded.error;
  ASSERT_EQ(loaded.document.cameras.size(), 1U);
  EXPECT_EQ(loaded.document.cameras.front().outputWidth, 2560U);
  EXPECT_EQ(loaded.document.cameras.front().outputHeight, 1440U);
  EXPECT_EQ(loaded.document.cameras.front().name, "Main View");
  EXPECT_EQ(loaded.document.cameras.front().rotation, camera.rotation);
  EXPECT_EQ(loaded.document.cameras.front().scale, camera.scale);
  EXPECT_EQ(loaded.document.cameras.front().priority, -4);
  EXPECT_FALSE(loaded.document.cameras.front().active);
  ASSERT_EQ(loaded.document.lights.size(), 1U);
  EXPECT_EQ(loaded.document.lights.front().name, "Fill Light");
  EXPECT_EQ(loaded.document.lights.front().position, light.position);
  EXPECT_EQ(loaded.document.lights.front().rotation, light.rotation);
  EXPECT_EQ(loaded.document.lights.front().scale, light.scale);
  ASSERT_EQ(loaded.document.entities.size(), 1U);
  EXPECT_FALSE(loaded.document.entities.front().meshVisible);

  const auto legacyPath = project.Root() / "legacy-camera.scene.json";
  {
    std::ofstream legacy(legacyPath, std::ios::binary | std::ios::trunc);
    legacy
        << R"({"schemaVersion":1,"unitsPerMeter":1,"cameras":[{"id":9,"position":[0,0,0],"rotation":[0,0,0,1],"fovRadians":1,"nearPlane":0.1,"farPlane":100}]})";
  }
  const auto legacy = assets.LoadScene("legacy-camera.scene.json");
  ASSERT_TRUE(legacy) << legacy.error;
  ASSERT_EQ(legacy.document.cameras.size(), 1U);
  EXPECT_EQ(legacy.document.cameras.front().outputWidth, 1920U);
  EXPECT_EQ(legacy.document.cameras.front().outputHeight, 1080U);
  EXPECT_EQ(legacy.document.cameras.front().name, "Camera");
  EXPECT_EQ(legacy.document.cameras.front().scale, (std::array<float, 3>{1.0F, 1.0F, 1.0F}));
  EXPECT_EQ(legacy.document.cameras.front().priority, 0);
  EXPECT_TRUE(legacy.document.cameras.front().active);
}

TEST(ScenePersistenceTests, FailedAtomicReplacementPreservesDestinationAndRemovesTemporaryFile) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  const auto destination = project.Root() / "blocked.scene.json";
  std::filesystem::create_directories(destination);
  const auto marker = destination / "preserve.txt";
  {
    std::ofstream output(marker, std::ios::binary | std::ios::trunc);
    output << "keep existing destination";
    ASSERT_TRUE(output.good());
  }

  Engine::SceneDocument document;
  std::string error;
  EXPECT_FALSE(assets.SaveScene("blocked.scene.json", document, error));
  EXPECT_NE(error.find("atomic replacement failed"), std::string::npos) << error;

  std::ifstream input(marker, std::ios::binary);
  EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()),
            "keep existing destination");
  for (const auto& entry : std::filesystem::directory_iterator(project.Root())) {
    EXPECT_FALSE(entry.path().filename().string().starts_with("blocked.scene.json.tmp."));
  }
}

TEST(ScenePersistenceTests, MalformedInputReturnsDiagnostic) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  {
    std::ofstream malformed(project.Root() / "bad.scene.json", std::ios::binary);
    malformed << "{not valid JSON";
  }

  const auto loaded = assets.LoadScene("bad.scene.json");
  EXPECT_FALSE(loaded);
  EXPECT_NE(loaded.error.find("bad.scene.json"), std::string::npos);
}

TEST(ScenePersistenceTests, ReportsMissingAndOutOfProjectScenePaths) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());

  const auto missing = assets.LoadScene("missing.scene.json");
  EXPECT_FALSE(missing);
  EXPECT_NE(missing.error.find("could not open scene file"), std::string::npos);

  const auto outside = assets.LoadScene("../outside.scene.json");
  EXPECT_FALSE(outside);
  EXPECT_NE(outside.error.find("outside the project root"), std::string::npos);
}

TEST(ScenePersistenceTests, MigratesLegacySpecularGlossinessBrdfToWorkflow) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  {
    std::ofstream scene(project.Root() / "legacy-material.scene.json", std::ios::binary);
    scene
        << R"({"schemaVersion":1,"unitsPerMeter":1.0,"entities":[{"id":1,"primitiveType":"cube","material":{"brdfModel":"specularGlossiness"}}]})";
  }

  const auto loaded = assets.LoadScene("legacy-material.scene.json");
  ASSERT_TRUE(loaded) << loaded.error;
  ASSERT_EQ(loaded.document.entities.size(), 1U);
  ASSERT_TRUE(loaded.document.entities.front().material.has_value());
  EXPECT_EQ(loaded.document.entities.front().material->brdfModel, "ggx");
  EXPECT_EQ(loaded.document.entities.front().material->workflow, "specularGlossiness");
  ASSERT_EQ(loaded.warnings.size(), 1U);
  EXPECT_NE(loaded.warnings.front().find("migrated specularGlossiness"), std::string::npos);
}

TEST(ScenePersistenceTests, RejectsUnsupportedSchemaVersion) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  {
    std::ofstream scene(project.Root() / "future-schema.scene.json", std::ios::binary);
    scene << R"({"schemaVersion":4,"unitsPerMeter":1.0,"entities":[]})";
  }

  const auto loaded = assets.LoadScene("future-schema.scene.json");
  EXPECT_FALSE(loaded);
  EXPECT_NE(loaded.error.find("unsupported scene schema version 4"), std::string::npos);
}

TEST(ScenePersistenceTests, RejectsMalformedEntityLightAndCameraFields) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  const auto checkRejected = [&assets, &project](const std::string& name, const std::string& json,
                                                 const std::string& expectedError) {
    {
      std::ofstream scene(project.Root() / name, std::ios::binary);
      scene << json;
    }
    const auto loaded = assets.LoadScene(name);
    EXPECT_FALSE(loaded) << name;
    EXPECT_NE(loaded.error.find(expectedError), std::string::npos) << name << ": " << loaded.error;
  };
  const auto root = [](const std::string& fields) {
    return "{\"schemaVersion\":1,\"unitsPerMeter\":1.0," + fields + "}";
  };

  checkRejected("entities-not-array.json", root("\"entities\":{}"), "entities must be an array");
  checkRejected("entity-not-object.json", root("\"entities\":[null]"), "entities[0] must be an object");
  checkRejected("invalid-entity-id.json", root("\"entities\":[{\"id\":0}]"), "id must be a unique positive integer");
  checkRejected("missing-entity-id.json", root("\"entities\":[{\"primitiveType\":\"cube\"}]"),
                "id must be a unique positive integer");
  checkRejected("string-entity-id.json", root("\"entities\":[{\"id\":\"1\",\"primitiveType\":\"cube\"}]"),
                "id must be a unique positive integer");
  checkRejected("duplicate-entity-id.json",
                root("\"entities\":[{\"id\":1,\"primitiveType\":\"cube\"},"
                     "{\"id\":1,\"primitiveType\":\"cube\"}]"),
                "id must be a unique positive integer");
  checkRejected("invalid-primitive.json", root("\"entities\":[{\"id\":1,\"primitiveType\":\"torus\"}]"),
                "primitiveType is unsupported");
  checkRejected("invalid-material.json", root("\"entities\":[{\"id\":1,\"primitiveType\":\"cube\",\"material\":[]}]"),
                "material must be an object");
  checkRejected("invalid-render.json", root("\"entities\":[{\"id\":1,\"primitiveType\":\"cube\",\"render\":true}]"),
                "render must be an object");
  checkRejected("zero-rotation.json",
                root("\"entities\":[{\"id\":1,\"primitiveType\":\"cube\","
                     "\"rotation\":[0,0,0,0]}]"),
                "rotation must be a non-zero quaternion");
  checkRejected("invalid-entity-name.json", root("\"entities\":[{\"id\":1,\"name\":7}]"),
                "entities[0].name must be a string");
  checkRejected("invalid-model-guid.json", root("\"entities\":[{\"id\":1,\"modelGuid\":\"bad\"}]"),
                "modelGuid is not a valid GUID");
  checkRejected("invalid-guid-format.json",
                root("\"entities\":[{\"id\":1,\"modelGuid\":\"111111111111111111111111111111111111\"}]"),
                "modelGuid is not a valid GUID");
  checkRejected("invalid-primitive-type.json", root("\"entities\":[{\"id\":1,\"primitiveType\":7}]"),
                "primitiveType must be a string or null");
  checkRejected("entity-has-two-assets.json",
                root("\"entities\":[{\"id\":1,\"modelGuid\":"
                     "\"11111111-1111-4111-8111-111111111111\",\"primitiveType\":\"cube\"}]"),
                "cannot specify both modelGuid and primitiveType");
  checkRejected("invalid-material-overrides.json",
                root("\"entities\":[{\"id\":1,\"modelGuid\":"
                     "\"11111111-1111-4111-8111-111111111111\",\"materialOverrides\":{}}]"),
                "materialOverrides must be an array");
  checkRejected("invalid-material-override.json",
                root("\"entities\":[{\"id\":1,\"modelGuid\":"
                     "\"11111111-1111-4111-8111-111111111111\",\"materialOverrides\":[[]]}]"),
                "material must be an object");
  checkRejected("invalid-position.json",
                root("\"entities\":[{\"id\":1,\"primitiveType\":\"cube\",\"position\":[0,1]}]"),
                "position must be an array of 3 numbers");
  checkRejected("invalid-scale.json",
                root("\"entities\":[{\"id\":1,\"primitiveType\":\"cube\",\"scale\":[1,\"x\",1]}]"),
                "scale contains a non-numeric value");
  checkRejected("invalid-render-property.json",
                root("\"entities\":[{\"id\":1,\"primitiveType\":\"cube\","
                     "\"render\":{\"forceWireframe\":1}}]"),
                "render.forceWireframe must be boolean");
  checkRejected("lights-not-array.json", root("\"lights\":{}"), "lights must be an array");
  checkRejected("light-not-object.json", root("\"lights\":[null]"), "lights[0] must be an object");
  checkRejected("invalid-light-range.json",
                root("\"lights\":[{\"id\":1,\"direction\":[0,-1,0],\"position\":[0,0,0],"
                     "\"color\":[1,1,1],\"intensity\":1,\"range\":0,"
                     "\"innerAngleDegrees\":0,\"outerAngleDegrees\":45,\"shadowBias\":0,"
                     "\"shadowNormalBias\":0,\"usePosition\":false,\"castShadows\":true}]"),
                "out-of-range light settings");
  const std::string validLight =
      R"({"id":1,"direction":[0,-1,0],"position":[0,0,0],"color":[1,1,1],"intensity":1,"range":10,"innerAngleDegrees":0,"outerAngleDegrees":45,"shadowBias":0,"shadowNormalBias":0,"usePosition":false,"castShadows":true})";
  const auto replaceField = [](std::string json, const std::string& oldValue, const std::string& newValue) {
    const size_t position = json.find(oldValue);
    EXPECT_NE(position, std::string::npos);
    if (position != std::string::npos)
      json.replace(position, oldValue.size(), newValue);
    return json;
  };
  checkRejected("light-invalid-direction.json",
                root("\"lights\":[" + replaceField(validLight, "[0,-1,0]", "[0,0,0]") + "]"),
                "out-of-range light settings");
  checkRejected("light-negative-intensity.json",
                root("\"lights\":[" + replaceField(validLight, "\"intensity\":1", "\"intensity\":-1") + "]"),
                "out-of-range light settings");
  checkRejected(
      "light-invalid-cone.json",
      root("\"lights\":[" + replaceField(validLight, "\"outerAngleDegrees\":45", "\"outerAngleDegrees\":-1") + "]"),
      "out-of-range light settings");
  checkRejected("light-negative-shadow-bias.json",
                root("\"lights\":[" + replaceField(validLight, "\"shadowBias\":0", "\"shadowBias\":-1") + "]"),
                "out-of-range light settings");
  checkRejected(
      "light-invalid-use-position.json",
      root("\"lights\":[" + replaceField(validLight, "\"usePosition\":false", "\"usePosition\":\"false\"") + "]"),
      "usePosition must be boolean");
  checkRejected("cameras-not-array.json", root("\"cameras\":{}"), "cameras must be an array");
  checkRejected("camera-not-object.json", root("\"cameras\":[null]"), "cameras[0] must be an object");
  checkRejected("invalid-camera-projection.json",
                root("\"cameras\":[{\"id\":1,\"position\":[0,0,0],\"rotation\":[0,0,0,1],"
                     "\"fovRadians\":0,\"nearPlane\":0.1,\"farPlane\":100}]"),
                "invalid projection or rotation values");
  const std::string validCamera =
      R"({"id":1,"position":[0,0,0],"rotation":[0,0,0,1],"fovRadians":1,"nearPlane":0.1,"farPlane":100})";
  checkRejected(
      "camera-zero-width.json",
      root("\"cameras\":[" + replaceField(validCamera, "\"farPlane\":100", "\"farPlane\":100,\"outputWidth\":0") + "]"),
      "positive 32-bit integer");
  checkRejected("camera-fractional-height.json",
                root("\"cameras\":[" +
                     replaceField(validCamera, "\"farPlane\":100", "\"farPlane\":100,\"outputHeight\":720.5") + "]"),
                "positive 32-bit integer");
  checkRejected("camera-near-plane.json",
                root("\"cameras\":[" + replaceField(validCamera, "\"nearPlane\":0.1", "\"nearPlane\":0") + "]"),
                "invalid projection or rotation values");
  checkRejected("camera-far-plane.json",
                root("\"cameras\":[" + replaceField(validCamera, "\"farPlane\":100", "\"farPlane\":0.05") + "]"),
                "invalid projection or rotation values");
  checkRejected("camera-zero-rotation.json",
                root("\"cameras\":[" + replaceField(validCamera, "[0,0,0,1]", "[0,0,0,0]") + "]"),
                "invalid projection or rotation values");
}

TEST(ScenePersistenceTests, WarnsForUnknownFieldsAndNormalizesUnsupportedLegacyMaterial) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  {
    std::ofstream scene(project.Root() / "unknown-fields.scene.json", std::ios::binary);
    scene
        << R"({"schemaVersion":1,"unitsPerMeter":1.0,"extraRoot":true,"entities":[{"id":1,"primitiveType":"cube","extraEntity":7,"material":{"brdfModel":"neural","extraMaterial":false}}]})";
  }

  const auto loaded = assets.LoadScene("unknown-fields.scene.json");
  ASSERT_TRUE(loaded) << loaded.error;
  ASSERT_EQ(loaded.document.entities.size(), 1U);
  ASSERT_TRUE(loaded.document.entities.front().material.has_value());
  EXPECT_EQ(loaded.document.entities.front().material->brdfModel, "ggx");
  ASSERT_EQ(loaded.warnings.size(), 4U);
  EXPECT_NE(loaded.warnings[0].find("extraRoot"), std::string::npos);
  EXPECT_NE(loaded.warnings[1].find("extraEntity"), std::string::npos);
  EXPECT_NE(loaded.warnings[2].find("extraMaterial"), std::string::npos);
  EXPECT_NE(loaded.warnings[3].find("unsupported neural BRDF"), std::string::npos);
}

TEST(ScenePersistenceTests, RejectsInvalidSchemaAndMaterialValues) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  const auto checkRejected = [&assets, &project](const std::string& name, const std::string& json,
                                                 const std::string& expectedError) {
    {
      std::ofstream scene(project.Root() / name, std::ios::binary | std::ios::trunc);
      scene << json;
    }
    const auto loaded = assets.LoadScene(name);
    EXPECT_FALSE(loaded) << name;
    EXPECT_NE(loaded.error.find(expectedError), std::string::npos) << name << ": " << loaded.error;
  };
  const auto sceneWith = [](const std::string& fields) {
    return "{\"schemaVersion\":1,\"unitsPerMeter\":1.0," + fields + "}";
  };
  const auto sceneWithMaterial = [&sceneWith](const std::string& material) {
    return sceneWith("\"entities\":[{\"id\":1,\"primitiveType\":\"cube\",\"material\":" + material + "}]");
  };

  checkRejected("root-array.json", "[]", "scene root must be an object");
  checkRejected("missing-schema.json", "{\"unitsPerMeter\":1.0}", "schemaVersion must be an unsigned integer");
  checkRejected("signed-schema.json", "{\"schemaVersion\":-1,\"unitsPerMeter\":1.0}",
                "schemaVersion must be an unsigned integer");
  checkRejected("bad-coordinate-system.json", sceneWith("\"coordinateSystem\":\"left-handed\""),
                "coordinateSystem must be 'right-handed-y-up'");
  checkRejected("zero-units.json", "{\"schemaVersion\":1,\"unitsPerMeter\":0}", "unitsPerMeter must be positive");

  checkRejected("bad-material-array.json", sceneWithMaterial("{\"baseColor\":[1,2]}"),
                "baseColor must be an array of 4 numbers");
  checkRejected("bad-material-array-type.json", sceneWithMaterial("{\"baseColor\":\"red\"}"),
                "baseColor must be an array of 4 numbers");
  checkRejected("bad-material-array-value.json", sceneWithMaterial("{\"baseColor\":[1,\"red\",0,1]}"),
                "baseColor contains a non-numeric value");
  checkRejected("bad-material-float-range.json", sceneWithMaterial("{\"baseColor\":[1e40,0,0,1]}"),
                "baseColor contains a non-finite or unrepresentable value");
  checkRejected("bad-material-number.json", sceneWithMaterial("{\"metallic\":\"high\"}"), "metallic must be numeric");
  checkRejected("bad-material-number-range.json", sceneWithMaterial("{\"metallic\":1e40}"),
                "metallic contains a non-finite or unrepresentable value");
  checkRejected("bad-material-brdf-type.json", sceneWithMaterial("{\"brdfModel\":7}"), "brdfModel must be a string");
  checkRejected("bad-material-brdf-value.json", sceneWithMaterial("{\"brdfModel\":\"phong\"}"),
                "brdfModel has an unsupported value");
  checkRejected("bad-material-workflow.json", sceneWithMaterial("{\"workflow\":\"unknown\"}"),
                "workflow has an unsupported value");
  checkRejected("bad-material-specular-mode.json", sceneWithMaterial("{\"specularMode\":\"unknown\"}"),
                "specularMode has an unsupported value");
  checkRejected("bad-material-alpha-mode.json", sceneWithMaterial("{\"alphaMode\":\"unknown\"}"),
                "alphaMode has an unsupported value");
  checkRejected("bad-material-alpha-cutoff.json", sceneWithMaterial("{\"alphaCutoff\":1.5}"),
                "alphaCutoff must be within [0, 1]");
  checkRejected("bad-material-metallic.json", sceneWithMaterial("{\"metallic\":1.5}"), "out-of-range BRDF settings");
  checkRejected("bad-material-color.json", sceneWithMaterial("{\"baseColor\":[1.5,0,0,1]}"),
                "baseColor values must be within [0, 1]");
  checkRejected("bad-material-emissive.json", sceneWithMaterial("{\"emissive\":[0,-1,0]}"),
                "emissive values must be non-negative");
  checkRejected("bad-material-f0.json", sceneWithMaterial("{\"specularF0\":[0,1.5,0]}"),
                "specularF0 values must be within [0, 1]");
  checkRejected("bad-material-guid.json", sceneWithMaterial("{\"baseColorTextureGuid\":\"not-a-guid\"}"),
                "baseColorTextureGuid is not a valid GUID");
  checkRejected("bad-material-guid-type.json", sceneWithMaterial("{\"normalTextureGuid\":5}"),
                "normalTextureGuid must be a GUID string or null");
}

} // namespace
