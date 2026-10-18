#include "panels/include/AnalyticalFrameArchive.h"

#include "scene/include/SceneDocument.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using Json = nlohmann::json;

constexpr std::array<uint8_t, 8> Magic{'A', 'F', 'R', 'A', 'M', 'E', '\r', '\n'};
constexpr uint32_t FormatVersion = 1;
constexpr uint64_t MaximumManifestBytes = 64ULL * 1024ULL * 1024ULL;
constexpr uint64_t MaximumArchiveBytes = 16ULL * 1024U * 1024U * 1024U;
constexpr uint64_t HeaderBytes = Magic.size() + sizeof(uint64_t);

auto Fail(std::string message) -> Engine::Error { return Engine::MakeError(std::move(message)); }

auto ModelIdentifier(Engine::Renderer::BrdfModel model) -> std::string_view {
  using Engine::Renderer::BrdfModel;
  switch (model) {
  case BrdfModel::Legacy:
    return "legacy";
  case BrdfModel::GGX:
    return "ggx";
  case BrdfModel::GTR:
    return "gtr";
  case BrdfModel::Beckmann:
    return "beckmann";
  case BrdfModel::Gauss:
    return "gauss";
  case BrdfModel::BlinnPhong:
    return "blinnPhong";
  case BrdfModel::AshikhminShirley:
    return "ashikhminShirley";
  case BrdfModel::Schlick:
    return "schlick";
  case BrdfModel::Sawicki:
    return "sawicki";
  }
  return "unknown";
}

auto ParseModel(const Json& value) -> std::optional<Engine::Renderer::BrdfModel> {
  if (!value.is_string()) {
    return std::nullopt;
  }
  const std::string& name = value.get_ref<const std::string&>();
  using Engine::Renderer::BrdfModel;
  constexpr std::array models{BrdfModel::Legacy,           BrdfModel::GGX,     BrdfModel::GTR,
                              BrdfModel::Beckmann,         BrdfModel::Gauss,   BrdfModel::BlinnPhong,
                              BrdfModel::AshikhminShirley, BrdfModel::Schlick, BrdfModel::Sawicki};
  for (const auto model : models) {
    if (name == ModelIdentifier(model)) {
      return model;
    }
  }
  return std::nullopt;
}

auto AovIdentifier(Engine::RenderAov aov) -> std::string_view {
  switch (aov) {
  case Engine::RenderAov::FinalColor:
    return "finalColor";
  case Engine::RenderAov::Depth:
    return "depth";
  case Engine::RenderAov::WorldNormal:
    return "worldNormal";
  case Engine::RenderAov::SpecularLighting:
    return "specularLighting";
  case Engine::RenderAov::DiffuseLighting:
    return "diffuseLighting";
  }
  return "unknown";
}

auto ParseAov(const Json& value) -> std::optional<Engine::RenderAov> {
  if (!value.is_string()) {
    return std::nullopt;
  }
  const std::string& name = value.get_ref<const std::string&>();
  constexpr std::array aovs{Engine::RenderAov::FinalColor, Engine::RenderAov::Depth, Engine::RenderAov::WorldNormal,
                            Engine::RenderAov::SpecularLighting, Engine::RenderAov::DiffuseLighting};
  for (const auto aov : aovs) {
    if (name == AovIdentifier(aov)) {
      return aov;
    }
  }
  return std::nullopt;
}

template <size_t Size> auto ReadFloatArray(const Json& value, std::array<float, Size>& output) -> bool {
  if (!value.is_array() || value.size() != Size) {
    return false;
  }
  for (size_t index = 0; index < Size; ++index) {
    if (!value.at(index).is_number()) {
      return false;
    }
    const double sample = value.at(index).get<double>();
    if (!std::isfinite(sample) || sample < -std::numeric_limits<float>::max() ||
        sample > std::numeric_limits<float>::max()) {
      return false;
    }
    output.at(index) = static_cast<float>(sample);
  }
  return true;
}

auto ReadFiniteFloat(const Json& object, std::string_view key, float& output) -> bool {
  const auto found = object.find(key);
  if (found == object.end() || !found->is_number()) {
    return false;
  }
  const double value = found->get<double>();
  if (!std::isfinite(value) || value < -std::numeric_limits<float>::max() ||
      value > std::numeric_limits<float>::max()) {
    return false;
  }
  output = static_cast<float>(value);
  return true;
}

auto MaterialDocumentJson(const Engine::SceneMaterialDocument& material) -> Json {
  return {{"baseColor", material.baseColor},
          {"emissive", material.emissive},
          {"metallic", material.metallic},
          {"roughness", material.roughness},
          {"gtrGamma", material.gtrGamma},
          {"anisotropicRoughnessU", material.anisotropicRoughnessU},
          {"anisotropicRoughnessV", material.anisotropicRoughnessV},
          {"ior", material.ior},
          {"specularF0", material.specularF0},
          {"brdfModel", material.brdfModel},
          {"workflow", material.workflow},
          {"specularMode", material.specularMode},
          {"alphaMode", material.alphaMode},
          {"alphaCutoff", material.alphaCutoff},
          {"doubleSided", material.doubleSided},
          {"baseColorTextureGuid", material.baseColorTextureGuid},
          {"normalTextureGuid", material.normalTextureGuid}};
}

auto ParseMaterialDocument(const Json& value) -> Engine::Result<Engine::SceneMaterialDocument> {
  if (!value.is_object()) {
    return std::unexpected(Fail("Analytical Frame source material must be an object"));
  }
  Engine::SceneMaterialDocument material;
  if (!ReadFloatArray(value.value("baseColor", Json{}), material.baseColor) ||
      !ReadFloatArray(value.value("emissive", Json{}), material.emissive) ||
      !ReadFloatArray(value.value("specularF0", Json{}), material.specularF0)) {
    return std::unexpected(Fail("Analytical Frame source material contains an invalid color"));
  }
  if (!ReadFiniteFloat(value, "metallic", material.metallic) ||
      !ReadFiniteFloat(value, "roughness", material.roughness) ||
      !ReadFiniteFloat(value, "gtrGamma", material.gtrGamma) ||
      !ReadFiniteFloat(value, "anisotropicRoughnessU", material.anisotropicRoughnessU) ||
      !ReadFiniteFloat(value, "anisotropicRoughnessV", material.anisotropicRoughnessV) ||
      !ReadFiniteFloat(value, "ior", material.ior) || !ReadFiniteFloat(value, "alphaCutoff", material.alphaCutoff)) {
    return std::unexpected(Fail("Analytical Frame source material contains an invalid numeric setting"));
  }
  try {
    material.brdfModel = value.at("brdfModel").get<std::string>();
    material.workflow = value.at("workflow").get<std::string>();
    material.specularMode = value.at("specularMode").get<std::string>();
    material.alphaMode = value.at("alphaMode").get<std::string>();
    material.doubleSided = value.at("doubleSided").get<bool>();
    material.baseColorTextureGuid = value.at("baseColorTextureGuid").get<std::optional<std::string>>();
    material.normalTextureGuid = value.at("normalTextureGuid").get<std::optional<std::string>>();
  } catch (const Json::exception&) {
    return std::unexpected(Fail("Analytical Frame source material is incomplete or malformed"));
  }
  return material;
}

auto MaterialPropertiesJson(const Engine::Renderer::MaterialProperties& material) -> Json {
  Json transforms = Json::array();
  for (const auto& transform : material.TextureTransforms) {
    transforms.push_back({{"offset", {transform.Offset.x, transform.Offset.y}},
                          {"scale", {transform.Scale.x, transform.Scale.y}},
                          {"rotation", transform.Rotation}});
  }
  return {{"baseColor", {material.BaseColor.x, material.BaseColor.y, material.BaseColor.z, material.BaseColor.w}},
          {"emissive", {material.Emissive.x, material.Emissive.y, material.Emissive.z}},
          {"metallic", material.Metallic},
          {"roughness", material.Roughness},
          {"gtrGamma", material.GtrGamma},
          {"anisotropicRoughnessU", material.AnisotropicRoughnessU},
          {"anisotropicRoughnessV", material.AnisotropicRoughnessV},
          {"ior", material.Ior},
          {"specularF0", {material.SpecularF0.x, material.SpecularF0.y, material.SpecularF0.z}},
          {"model", ModelIdentifier(material.Model)},
          {"workflow", static_cast<uint32_t>(material.Workflow)},
          {"specularMode", static_cast<uint32_t>(material.SpecularMode)},
          {"alphaMode", static_cast<uint32_t>(material.AlphaMode)},
          {"alphaCutoff", material.AlphaCutoff},
          {"doubleSided", material.DoubleSided},
          {"baseColorTexCoord", material.BaseColorTexCoord},
          {"normalTexCoord", material.NormalTexCoord},
          {"metallicRoughnessTexCoord", material.MetallicRoughnessTexCoord},
          {"occlusionTexCoord", material.OcclusionTexCoord},
          {"emissiveTexCoord", material.EmissiveTexCoord},
          {"normalScale", material.NormalScale},
          {"occlusionStrength", material.OcclusionStrength},
          {"textureTransforms", std::move(transforms)}};
}

auto ParseMaterialProperties(const Json& value) -> Engine::Result<Engine::Renderer::MaterialProperties> {
  using Engine::Renderer::MaterialProperties;
  if (!value.is_object()) {
    return std::unexpected(Fail("Analytical Frame variant material must be an object"));
  }
  MaterialProperties material;
  std::array<float, 4> baseColor{};
  std::array<float, 3> emissive{};
  std::array<float, 3> specularF0{};
  if (!ReadFloatArray(value.value("baseColor", Json{}), baseColor) ||
      !ReadFloatArray(value.value("emissive", Json{}), emissive) ||
      !ReadFloatArray(value.value("specularF0", Json{}), specularF0) ||
      !ReadFiniteFloat(value, "metallic", material.Metallic) ||
      !ReadFiniteFloat(value, "roughness", material.Roughness) ||
      !ReadFiniteFloat(value, "gtrGamma", material.GtrGamma) ||
      !ReadFiniteFloat(value, "anisotropicRoughnessU", material.AnisotropicRoughnessU) ||
      !ReadFiniteFloat(value, "anisotropicRoughnessV", material.AnisotropicRoughnessV) ||
      !ReadFiniteFloat(value, "ior", material.Ior) || !ReadFiniteFloat(value, "alphaCutoff", material.AlphaCutoff) ||
      !ReadFiniteFloat(value, "normalScale", material.NormalScale) ||
      !ReadFiniteFloat(value, "occlusionStrength", material.OcclusionStrength)) {
    return std::unexpected(Fail("Analytical Frame variant material has missing or invalid numeric properties"));
  }
  try {
    const auto model = ParseModel(value.at("model"));
    if (!model) {
      return std::unexpected(Fail("Analytical Frame variant uses an unsupported BRDF model"));
    }
    material.Model = *model;
    const uint32_t workflow = value.at("workflow").get<uint32_t>();
    const uint32_t specularMode = value.at("specularMode").get<uint32_t>();
    const uint32_t alphaMode = value.at("alphaMode").get<uint32_t>();
    if (workflow > static_cast<uint32_t>(Engine::Renderer::MaterialWorkflow::SpecularGlossiness) ||
        specularMode > static_cast<uint32_t>(Engine::Renderer::DielectricSpecularMode::ExplicitF0) ||
        alphaMode > static_cast<uint32_t>(Engine::Renderer::MaterialAlphaMode::Blend)) {
      return std::unexpected(Fail("Analytical Frame variant material contains an unsupported enum value"));
    }
    material.Workflow = static_cast<Engine::Renderer::MaterialWorkflow>(workflow);
    material.SpecularMode = static_cast<Engine::Renderer::DielectricSpecularMode>(specularMode);
    material.AlphaMode = static_cast<Engine::Renderer::MaterialAlphaMode>(alphaMode);
    material.DoubleSided = value.at("doubleSided").get<bool>();
    material.BaseColorTexCoord = value.at("baseColorTexCoord").get<uint32_t>();
    material.NormalTexCoord = value.at("normalTexCoord").get<uint32_t>();
    material.MetallicRoughnessTexCoord = value.at("metallicRoughnessTexCoord").get<uint32_t>();
    material.OcclusionTexCoord = value.at("occlusionTexCoord").get<uint32_t>();
    material.EmissiveTexCoord = value.at("emissiveTexCoord").get<uint32_t>();
    const Json& transforms = value.at("textureTransforms");
    if (!transforms.is_array() || transforms.size() != std::size(material.TextureTransforms)) {
      return std::unexpected(Fail("Analytical Frame texture transform list is malformed"));
    }
    for (size_t index = 0; index < std::size(material.TextureTransforms); ++index) {
      auto& transform = material.TextureTransforms[index];
      std::array<float, 2> offset{};
      std::array<float, 2> scale{};
      const Json& entry = transforms.at(index);
      if (!ReadFloatArray(entry.value("offset", Json{}), offset) ||
          !ReadFloatArray(entry.value("scale", Json{}), scale) ||
          !ReadFiniteFloat(entry, "rotation", transform.Rotation)) {
        return std::unexpected(Fail("Analytical Frame texture transform contains invalid values"));
      }
      transform.Offset = {offset[0], offset[1]};
      transform.Scale = {scale[0], scale[1]};
    }
  } catch (const Json::exception&) {
    return std::unexpected(Fail("Analytical Frame variant material is incomplete or malformed"));
  }
  material.BaseColor = {baseColor[0], baseColor[1], baseColor[2], baseColor[3]};
  material.Emissive = {emissive[0], emissive[1], emissive[2]};
  material.SpecularF0 = {specularF0[0], specularF0[1], specularF0[2]};
  return material;
}

auto SourcePropertiesJson(const Engine::Renderer::MaterialProperties& material) -> Json {
  return MaterialPropertiesJson(material);
}

auto ParseSourceProperties(const Json& value) -> Engine::Result<Engine::Renderer::MaterialProperties> {
  return ParseMaterialProperties(value);
}

auto OverridesJson(const Engine::Renderer::BrdfAnalysis::BrdfAnalysisOverrides& overrides) -> Json {
  Json result = Json::object();
  if (overrides.OutputResolution) {
    result["outputResolution"] = {{"width", overrides.OutputResolution->Width},
                                  {"height", overrides.OutputResolution->Height}};
  } else {
    result["outputResolution"] = nullptr;
  }
  result["roughness"] = overrides.Roughness ? Json(*overrides.Roughness) : Json(nullptr);
  result["gtrGamma"] = overrides.GtrGamma ? Json(*overrides.GtrGamma) : Json(nullptr);
  return result;
}

auto ParseOverrides(const Json& value) -> Engine::Result<Engine::Renderer::BrdfAnalysis::BrdfAnalysisOverrides> {
  using Overrides = Engine::Renderer::BrdfAnalysis::BrdfAnalysisOverrides;
  if (!value.is_object()) {
    return std::unexpected(Fail("Analytical Frame overrides must be an object"));
  }
  Overrides result;
  try {
    const Json& resolution = value.at("outputResolution");
    if (!resolution.is_null()) {
      const uint32_t width = resolution.at("width").get<uint32_t>();
      const uint32_t height = resolution.at("height").get<uint32_t>();
      if (width == 0 || height == 0) {
        return std::unexpected(Fail("Analytical Frame output resolution override is invalid"));
      }
      result.OutputResolution = Overrides::Resolution{width, height};
    }
    const Json& roughness = value.at("roughness");
    if (!roughness.is_null()) {
      float parsed = 0.0F;
      if (!ReadFiniteFloat(Json{{"value", roughness}}, "value", parsed) || parsed < 0.0F || parsed > 1.0F) {
        return std::unexpected(Fail("Analytical Frame roughness override is invalid"));
      }
      result.Roughness = parsed;
    }
    const Json& gamma = value.at("gtrGamma");
    if (!gamma.is_null()) {
      float parsed = 0.0F;
      if (!ReadFiniteFloat(Json{{"value", gamma}}, "value", parsed) || parsed < 1.0F || parsed > 8.0F) {
        return std::unexpected(Fail("Analytical Frame GTR gamma override is invalid"));
      }
      result.GtrGamma = parsed;
    }
  } catch (const Json::exception&) {
    return std::unexpected(Fail("Analytical Frame overrides are incomplete or malformed"));
  }
  return result;
}

auto CameraJson(const Engine::Renderer::CameraView& camera) -> Json {
  return {{"position", camera.Position},
          {"rotationXyzw", camera.Rotation},
          {"fieldOfViewDegrees", camera.FieldOfViewDegrees},
          {"nearPlane", camera.NearPlane},
          {"farPlane", camera.FarPlane}};
}

auto ParseCamera(const Json& value) -> Engine::Result<Engine::Renderer::CameraView> {
  Engine::Renderer::CameraView result;
  if (!value.is_object() || !ReadFloatArray(value.value("position", Json{}), result.Position) ||
      !ReadFloatArray(value.value("rotationXyzw", Json{}), result.Rotation) ||
      !ReadFiniteFloat(value, "fieldOfViewDegrees", result.FieldOfViewDegrees) ||
      !ReadFiniteFloat(value, "nearPlane", result.NearPlane) || !ReadFiniteFloat(value, "farPlane", result.FarPlane) ||
      result.FieldOfViewDegrees <= 0.0F || result.FieldOfViewDegrees >= 180.0F || result.NearPlane <= 0.0F ||
      result.FarPlane <= result.NearPlane) {
    return std::unexpected(Fail("Analytical Frame camera settings are invalid"));
  }
  return result;
}

auto SerializeScene(const Engine::SceneDocument& scene) -> Engine::Result<Json> {
  std::string text;
  std::string error;
  if (!Engine::SceneSerializer::SerializeJson(scene, text, error)) {
    return std::unexpected(Fail("Could not serialize Analytical Frame scene: " + error));
  }
  try {
    return Json::parse(text);
  } catch (const Json::exception& exception) {
    return std::unexpected(Fail(std::string("Could not parse serialized scene: ") + exception.what()));
  }
}

auto MakeTempScenePath() -> Engine::Result<std::filesystem::path> {
  std::error_code error;
  const auto directory = std::filesystem::temp_directory_path(error);
  if (error) {
    return std::unexpected(Fail("Could not locate a temporary directory for scene validation: " + error.message()));
  }
  for (uint32_t attempt = 0; attempt < 32; ++attempt) {
    auto path = directory / ("analytical-frame-scene-" +
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                             std::to_string(attempt) + ".scene");
    if (!std::filesystem::exists(path, error) && !error) {
      return path;
    }
    error.clear();
  }
  return std::unexpected(Fail("Could not allocate a temporary scene path for archive validation"));
}

auto DeserializeScene(const Json& value) -> Engine::Result<Engine::SceneDocument> {
  if (!value.is_object()) {
    return std::unexpected(Fail("Analytical Frame scene manifest must be an object"));
  }
  auto path = MakeTempScenePath();
  if (!path) {
    return std::unexpected(path.error());
  }
  const auto cleanup = [&path]() {
    std::error_code ignored;
    std::filesystem::remove(*path, ignored);
  };
  {
    std::ofstream output(*path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return std::unexpected(Fail("Could not create temporary scene for archive validation"));
    }
    output << value.dump();
    if (!output) {
      cleanup();
      return std::unexpected(Fail("Could not write temporary scene for archive validation"));
    }
  }
  auto loaded = Engine::SceneSerializer::Load(*path);
  cleanup();
  if (!loaded) {
    return std::unexpected(Fail("Analytical Frame contains an invalid scene: " + loaded.error));
  }
  return std::move(loaded.document);
}

auto PropertiesFromSnapshot(const Engine::Renderer::MaterialProperties& source) -> Json {
  return SourcePropertiesJson(source);
}

auto SnapshotJson(const Editor::Panels::AnalyticalFrameSession& session) -> Engine::Result<Json> {
  auto sourceMaterial = ParseMaterialDocument(MaterialDocumentJson(session.Snapshot.SourceMaterial));
  auto sourceProperties = ParseSourceProperties(SourcePropertiesJson(session.Snapshot.SourceMaterialProperties));
  auto camera = ParseCamera(CameraJson(session.Snapshot.Camera));
  auto overrides = ParseOverrides(OverridesJson(session.Snapshot.FrameOverrides));
  if (!sourceMaterial || !sourceProperties || !camera || !overrides) {
    return std::unexpected(Fail("Analytical Frame snapshot contains invalid source material or render settings"));
  }
  if (session.Snapshot.Width == 0 || session.Snapshot.Height == 0 || session.Snapshot.TargetEntityId == 0) {
    return std::unexpected(Fail("Analytical Frame snapshot has invalid target or dimensions"));
  }
  const auto scene = SerializeScene(session.Snapshot.Scene);
  if (!scene) {
    return std::unexpected(scene.error());
  }
  const auto& snapshot = session.Snapshot;
  return Json{
      {"targetEntityId", snapshot.TargetEntityId},
      {"materialOverrideIndex", snapshot.MaterialOverrideIndex ? Json(*snapshot.MaterialOverrideIndex) : Json(nullptr)},
      {"scene", *scene},
      {"sourceMaterial", MaterialDocumentJson(snapshot.SourceMaterial)},
      {"sourceMaterialProperties", PropertiesFromSnapshot(snapshot.SourceMaterialProperties)},
      {"camera", CameraJson(snapshot.Camera)},
      {"width", snapshot.Width},
      {"height", snapshot.Height},
      {"shadowMapResolution", snapshot.ShadowMapResolution},
      {"maxShadowCastersPerScene", snapshot.MaxShadowCastersPerScene},
      {"frameOverrides", OverridesJson(snapshot.FrameOverrides)},
      {"postProcessCallbackPresent", session.PostProcessCallbackPresent || static_cast<bool>(snapshot.PostProcess)}};
}

auto ValidateVariantRequest(const Engine::Renderer::BrdfAnalysis::BrdfAnalysisSnapshot& snapshot,
                            const Editor::Panels::AnalyticalFrameVariant& variant) -> Engine::Status {
  const auto& request = variant.Render.Request;
  Engine::Renderer::MaterialProperties expectedMaterial = snapshot.SourceMaterialProperties;
  expectedMaterial.Model = request.Model;
  if (snapshot.FrameOverrides.Roughness) {
    expectedMaterial.Roughness = *snapshot.FrameOverrides.Roughness;
  }
  if (snapshot.FrameOverrides.GtrGamma) {
    expectedMaterial.GtrGamma = *snapshot.FrameOverrides.GtrGamma;
  }
  if (MaterialPropertiesJson(request.Material) != MaterialPropertiesJson(expectedMaterial) ||
      CameraJson(request.Camera) != CameraJson(snapshot.Camera) ||
      OverridesJson(request.FrameOverrides) != OverridesJson(snapshot.FrameOverrides) ||
      request.ShadowMapResolution != snapshot.ShadowMapResolution ||
      request.MaxShadowCastersPerScene != snapshot.MaxShadowCastersPerScene) {
    return std::unexpected(Fail("Analytical Frame variant render settings differ from the captured snapshot"));
  }
  const uint32_t expectedWidth =
      snapshot.FrameOverrides.OutputResolution ? snapshot.FrameOverrides.OutputResolution->Width : snapshot.Width;
  const uint32_t expectedHeight =
      snapshot.FrameOverrides.OutputResolution ? snapshot.FrameOverrides.OutputResolution->Height : snapshot.Height;
  if (request.Width != expectedWidth || request.Height != expectedHeight) {
    return std::unexpected(Fail("Analytical Frame variant dimensions differ from the captured resolution"));
  }
  Engine::SceneDocument expectedScene = snapshot.Scene;
  const auto entity =
      std::find_if(expectedScene.entities.begin(), expectedScene.entities.end(),
                   [&snapshot](const auto& candidate) { return candidate.id == snapshot.TargetEntityId; });
  if (entity == expectedScene.entities.end()) {
    return std::unexpected(Fail("Analytical Frame target entity is missing from the captured scene"));
  }
  Engine::SceneMaterialDocument expectedDocument = snapshot.SourceMaterial;
  expectedDocument.brdfModel = ModelIdentifier(request.Model);
  if (snapshot.FrameOverrides.Roughness) {
    expectedDocument.roughness = *snapshot.FrameOverrides.Roughness;
  }
  if (snapshot.FrameOverrides.GtrGamma) {
    expectedDocument.gtrGamma = *snapshot.FrameOverrides.GtrGamma;
  }
  if (snapshot.MaterialOverrideIndex) {
    const size_t index = *snapshot.MaterialOverrideIndex;
    if (index >= entity->materialOverrides.max_size()) {
      return std::unexpected(Fail("Analytical Frame material override index is invalid"));
    }
    if (entity->materialOverrides.size() <= index) {
      entity->materialOverrides.resize(index + 1U);
    }
    entity->materialOverrides[index] = std::move(expectedDocument);
  } else {
    entity->materialGuid.reset();
    entity->material = std::move(expectedDocument);
  }
  auto expectedSceneJson = SerializeScene(expectedScene);
  auto actualSceneJson = SerializeScene(request.Scene);
  if (!expectedSceneJson || !actualSceneJson || *expectedSceneJson != *actualSceneJson) {
    return std::unexpected(Fail("Analytical Frame variants do not share the captured scene inputs"));
  }
  return {};
}

auto ParseSnapshot(const Json& value, Editor::Panels::AnalyticalFrameSession& session) -> Engine::Status {
  if (!value.is_object()) {
    return std::unexpected(Fail("Analytical Frame snapshot must be an object"));
  }
  auto scene = DeserializeScene(value.value("scene", Json{}));
  if (!scene) {
    return std::unexpected(scene.error());
  }
  auto material = ParseMaterialDocument(value.value("sourceMaterial", Json{}));
  if (!material) {
    return std::unexpected(material.error());
  }
  auto properties = ParseSourceProperties(value.value("sourceMaterialProperties", Json{}));
  if (!properties) {
    return std::unexpected(properties.error());
  }
  auto camera = ParseCamera(value.value("camera", Json{}));
  if (!camera) {
    return std::unexpected(camera.error());
  }
  auto overrides = ParseOverrides(value.value("frameOverrides", Json{}));
  if (!overrides) {
    return std::unexpected(overrides.error());
  }
  try {
    auto& snapshot = session.Snapshot;
    snapshot.Scene = std::move(*scene);
    snapshot.TargetEntityId = value.at("targetEntityId").get<uint64_t>();
    if (!value.at("materialOverrideIndex").is_null()) {
      snapshot.MaterialOverrideIndex = value.at("materialOverrideIndex").get<size_t>();
    }
    snapshot.SourceMaterial = std::move(*material);
    snapshot.SourceMaterialProperties = *properties;
    snapshot.Camera = *camera;
    snapshot.Width = value.at("width").get<uint32_t>();
    snapshot.Height = value.at("height").get<uint32_t>();
    snapshot.ShadowMapResolution = value.at("shadowMapResolution").get<uint32_t>();
    snapshot.MaxShadowCastersPerScene = value.at("maxShadowCastersPerScene").get<uint32_t>();
    snapshot.FrameOverrides = *overrides;
    session.PostProcessCallbackPresent = value.at("postProcessCallbackPresent").get<bool>();
  } catch (const Json::exception&) {
    return std::unexpected(Fail("Analytical Frame snapshot is incomplete or malformed"));
  }
  if (session.Snapshot.Width == 0 || session.Snapshot.Height == 0 || session.Snapshot.TargetEntityId == 0) {
    return std::unexpected(Fail("Analytical Frame snapshot has invalid target or dimensions"));
  }
  return {};
}

auto ViewportStateValid(const Json& state, uint32_t width, uint32_t height) -> Engine::Status {
  if (!state.is_object()) {
    return std::unexpected(Fail("Analytical Frame viewport state must be an object"));
  }
  try {
    const auto selectedAov = ParseAov(state.at("SelectedAov"));
    if (!selectedAov || !ParseModel(state.at("SelectedModel")) || !ParseModel(state.at("CompareModelA")) ||
        !ParseModel(state.at("CompareModelB"))) {
      return std::unexpected(Fail("Analytical Frame viewport state contains an unsupported AOV or BRDF model"));
    }
    float zoom = 0.0F;
    float wipe = 0.0F;
    if (!ReadFiniteFloat(state, "Zoom", zoom) || zoom <= 0.0F || zoom > 1024.0F ||
        !ReadFiniteFloat(state, "WipePosition", wipe) || wipe < 0.0F || wipe > 1.0F) {
      return std::unexpected(Fail("Analytical Frame viewport zoom or wipe position is invalid"));
    }
    std::array<float, 2> pan{};
    if (!ReadFloatArray(state.at("PanOffsetPixels"), pan)) {
      return std::unexpected(Fail("Analytical Frame viewport pan offset is invalid"));
    }
    const Json& pinned = state.at("PinnedPixel");
    if (!pinned.is_null()) {
      if (!pinned.is_array() || pinned.size() != 2) {
        return std::unexpected(Fail("Analytical Frame pinned pixel is malformed"));
      }
      const uint32_t x = pinned.at(0).get<uint32_t>();
      const uint32_t y = pinned.at(1).get<uint32_t>();
      if (x >= width || y >= height) {
        return std::unexpected(Fail("Analytical Frame pinned pixel is outside the captured image"));
      }
    }
    const std::string mode = state.at("Mode").get<std::string>();
    if (mode != "Single" && mode != "Wipe" && mode != "RelativeLuminanceError") {
      return std::unexpected(Fail("Analytical Frame viewport comparison mode is unsupported"));
    }
    if (mode == "RelativeLuminanceError" && *selectedAov != Engine::RenderAov::FinalColor) {
      return std::unexpected(Fail("Analytical Frame relative luminance error requires the final-color AOV"));
    }
  } catch (const Json::exception&) {
    return std::unexpected(Fail("Analytical Frame viewport state is incomplete or malformed"));
  }
  return {};
}

auto CurveJson(const Engine::Renderer::BrdfAnalysis::BrdfResponseCurve& curve) -> Json {
  return {{"model", ModelIdentifier(curve.Model)},
          {"anglesDegrees", curve.AnglesDegrees},
          {"response", curve.Response},
          {"measurements",
           {{"peak", curve.Measurements.Peak},
            {"mean", curve.Measurements.Mean},
            {"integral", curve.Measurements.Integral}}}};
}

auto ParseCurve(const Json& value) -> Engine::Result<Engine::Renderer::BrdfAnalysis::BrdfResponseCurve> {
  using Engine::Renderer::BrdfAnalysis::BrdfResponseCurve;
  if (!value.is_object()) {
    return std::unexpected(Fail("Analytical Frame curve must be an object"));
  }
  BrdfResponseCurve curve;
  const auto model = ParseModel(value.value("model", Json{}));
  if (!model) {
    return std::unexpected(Fail("Analytical Frame curve uses an unsupported BRDF model"));
  }
  curve.Model = *model;
  try {
    curve.AnglesDegrees = value.at("anglesDegrees").get<std::vector<float>>();
    curve.Response = value.at("response").get<std::vector<float>>();
    const Json& measurements = value.at("measurements");
    if (!ReadFiniteFloat(measurements, "peak", curve.Measurements.Peak) ||
        !ReadFiniteFloat(measurements, "mean", curve.Measurements.Mean) ||
        !ReadFiniteFloat(measurements, "integral", curve.Measurements.Integral)) {
      return std::unexpected(Fail("Analytical Frame curve measurements are invalid"));
    }
  } catch (const Json::exception&) {
    return std::unexpected(Fail("Analytical Frame curve is incomplete or malformed"));
  }
  if (curve.AnglesDegrees.size() < 2 || curve.AnglesDegrees.size() != curve.Response.size() ||
      curve.AnglesDegrees.size() > 4096 || curve.Measurements.Peak < 0.0F || curve.Measurements.Mean < 0.0F ||
      curve.Measurements.Integral < 0.0F ||
      !std::all_of(
          curve.AnglesDegrees.begin(), curve.AnglesDegrees.end(), [](float x) { return std::isfinite(x); }) ||
      !std::all_of(
          curve.AnglesDegrees.begin(), curve.AnglesDegrees.end(), [](float x) { return x >= 0.0F && x <= 90.0F; }) ||
      !std::all_of(
          curve.Response.begin(), curve.Response.end(), [](float x) { return std::isfinite(x) && x >= 0.0F; })) {
    return std::unexpected(Fail("Analytical Frame curve sample arrays are invalid"));
  }
  return curve;
}

auto Crc32(std::span<const uint8_t> bytes) -> uint32_t {
  uint32_t crc = 0xFFFFFFFFU;
  for (const uint8_t byte : bytes) {
    crc ^= byte;
    for (uint32_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

void AppendU32(std::vector<uint8_t>& output, uint32_t value) {
  for (uint32_t shift = 0; shift < 32; shift += 8) {
    output.push_back(static_cast<uint8_t>((value >> shift) & 0xFFU));
  }
}

auto ReadU32(std::span<const uint8_t> bytes, size_t offset) -> uint32_t {
  return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16U) | (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

void WriteU64(std::ostream& output, uint64_t value) {
  for (uint32_t shift = 0; shift < 64; shift += 8) {
    output.put(static_cast<char>((value >> shift) & 0xFFU));
  }
}

auto ReadU64(std::span<const uint8_t> bytes, size_t offset) -> uint64_t {
  uint64_t value = 0;
  for (uint32_t shift = 0; shift < 64; shift += 8) {
    value |= static_cast<uint64_t>(bytes[offset + shift / 8]) << shift;
  }
  return value;
}

auto BuildManifest(const Editor::Panels::AnalyticalFrameSession& session, std::vector<uint8_t>& payload)
    -> Engine::Result<Json> {
  using namespace Engine::Renderer::BrdfAnalysis;
  auto snapshot = SnapshotJson(session);
  if (!snapshot) {
    return std::unexpected(snapshot.error());
  }
  if (session.TargetLabel.empty() || session.Variants.empty()) {
    return std::unexpected(Fail("Analytical Frame requires a target label and at least one completed variant"));
  }
  const uint32_t frameWidth = session.Variants.front().Render.Output.Width;
  const uint32_t frameHeight = session.Variants.front().Render.Output.Height;
  if (frameWidth == 0 || frameHeight == 0) {
    return std::unexpected(Fail("Analytical Frame captured output dimensions must be nonzero"));
  }
  const auto viewportStatus = ViewportStateValid(session.ViewportState, frameWidth, frameHeight);
  if (!viewportStatus) {
    return std::unexpected(viewportStatus.error());
  }
  Json manifest{{"formatVersion", FormatVersion},         {"targetLabel", session.TargetLabel}, {"snapshot", *snapshot},
                {"viewportState", session.ViewportState}, {"variants", Json::array()},          {"payloadBytes", 0U}};
  std::vector<Engine::Renderer::BrdfModel> models;
  for (const auto& variant : session.Variants) {
    const auto model = variant.Render.Request.Model;
    if (std::find(models.begin(), models.end(), model) != models.end() || variant.Curve.Model != model ||
        variant.Render.Request.Material.Model != model) {
      return std::unexpected(Fail("Analytical Frame contains duplicate or inconsistent BRDF variants"));
    }
    models.push_back(model);
    const auto requestStatus = ValidateVariantRequest(session.Snapshot, variant);
    if (!requestStatus) {
      return std::unexpected(requestStatus.error());
    }
    if (variant.Render.Request.Width == 0 || variant.Render.Request.Height == 0 ||
        variant.Render.Output.Width != variant.Render.Request.Width ||
        variant.Render.Output.Height != variant.Render.Request.Height || variant.Render.Output.Width != frameWidth ||
        variant.Render.Output.Height != frameHeight) {
      return std::unexpected(Fail("Analytical Frame variant dimensions are inconsistent"));
    }
    if (!ParseCurve(CurveJson(variant.Curve)) || !std::isfinite(variant.SceneLuminance.Mean) ||
        !std::isfinite(variant.SceneLuminance.Peak) || variant.SceneLuminance.Mean < 0.0F ||
        variant.SceneLuminance.Peak < variant.SceneLuminance.Mean) {
      return std::unexpected(Fail("Analytical Frame variant measurements are invalid"));
    }
    const Json curve = CurveJson(variant.Curve);
    Json record{{"model", ModelIdentifier(model)},
                {"requestScene", nullptr},
                {"material", MaterialPropertiesJson(variant.Render.Request.Material)},
                {"camera", CameraJson(variant.Render.Request.Camera)},
                {"width", variant.Render.Request.Width},
                {"height", variant.Render.Request.Height},
                {"shadowMapResolution", variant.Render.Request.ShadowMapResolution},
                {"maxShadowCastersPerScene", variant.Render.Request.MaxShadowCastersPerScene},
                {"frameOverrides", OverridesJson(variant.Render.Request.FrameOverrides)},
                {"curve", curve},
                {"sceneLuminance", {{"mean", variant.SceneLuminance.Mean}, {"peak", variant.SceneLuminance.Peak}}},
                {"planes", Json::array()}};
    auto requestScene = SerializeScene(variant.Render.Request.Scene);
    if (!requestScene) {
      return std::unexpected(requestScene.error());
    }
    record["requestScene"] = std::move(*requestScene);
    const auto planes = variant.Render.Output.GetPlanes();
    if (planes.size() != 5) {
      return std::unexpected(Fail("Analytical Frame variant does not contain all five AOV planes"));
    }
    constexpr std::array canonicalAovs{Engine::RenderAov::FinalColor, Engine::RenderAov::Depth,
                                       Engine::RenderAov::WorldNormal, Engine::RenderAov::SpecularLighting,
                                       Engine::RenderAov::DiffuseLighting};
    for (size_t planeIndex = 0; planeIndex < planes.size(); ++planeIndex) {
      const auto& plane = planes[planeIndex];
      const auto& descriptor = Engine::RenderOutput::GetDescriptor(canonicalAovs[planeIndex]);
      const uint64_t width = variant.Render.Output.Width;
      const uint64_t height = variant.Render.Output.Height;
      if (plane.Aov != canonicalAovs[planeIndex] || plane.ChannelCount != descriptor.ChannelCount ||
          width > std::numeric_limits<uint64_t>::max() / height ||
          width * height > std::numeric_limits<uint64_t>::max() / descriptor.ChannelCount ||
          plane.Pixels.size() != width * height * descriptor.ChannelCount ||
          plane.Pixels.size() > (std::numeric_limits<uint64_t>::max() / sizeof(float))) {
        return std::unexpected(Fail("Analytical Frame plane AOV, channel layout, or sample count is invalid"));
      }
      const uint64_t offset = payload.size();
      const uint64_t byteLength = static_cast<uint64_t>(plane.Pixels.size()) * sizeof(float);
      if (offset > MaximumArchiveBytes - HeaderBytes || byteLength > MaximumArchiveBytes - HeaderBytes - offset) {
        return std::unexpected(Fail("Analytical Frame captured buffers exceed the supported archive size"));
      }
      std::vector<uint8_t> planeBytes;
      if (byteLength > std::numeric_limits<size_t>::max()) {
        return std::unexpected(Fail("Analytical Frame plane exceeds addressable memory"));
      }
      planeBytes.reserve(static_cast<size_t>(byteLength));
      for (const float sample : plane.Pixels) {
        if (!std::isfinite(sample)) {
          return std::unexpected(Fail("Analytical Frame plane contains non-finite samples"));
        }
        AppendU32(planeBytes, std::bit_cast<uint32_t>(sample));
      }
      const uint32_t checksum = Crc32(planeBytes);
      payload.insert(payload.end(), planeBytes.begin(), planeBytes.end());
      record["planes"].push_back({{"aov", AovIdentifier(plane.Aov)},
                                  {"channels", plane.ChannelCount},
                                  {"offset", offset},
                                  {"byteLength", byteLength},
                                  {"sampleCount", plane.Pixels.size()},
                                  {"crc32", checksum}});
    }
    manifest["variants"].push_back(std::move(record));
  }
  manifest["payloadBytes"] = payload.size();
  return manifest;
}

auto ParseMeasurements(const Json& value, Engine::Renderer::BrdfAnalysis::SceneLuminanceMeasurements& result)
    -> Engine::Status {
  if (!value.is_object() || !ReadFiniteFloat(value, "mean", result.Mean) ||
      !ReadFiniteFloat(value, "peak", result.Peak) || result.Mean < 0.0F || result.Peak < result.Mean) {
    return std::unexpected(Fail("Analytical Frame scene luminance measurements are invalid"));
  }
  return {};
}

auto ReadPlane(const Json& metadata, std::span<const uint8_t> payload, uint32_t width, uint32_t height)
    -> Engine::Result<Engine::RenderOutputPlane> {
  Engine::RenderOutputPlane result;
  const auto aov = ParseAov(metadata.value("aov", Json{}));
  if (!aov) {
    return std::unexpected(Fail("Analytical Frame plane uses an unsupported AOV"));
  }
  const auto& descriptor = Engine::RenderOutput::GetDescriptor(*aov);
  try {
    result.Aov = *aov;
    result.ChannelCount = metadata.at("channels").get<uint32_t>();
    const uint64_t offset = metadata.at("offset").get<uint64_t>();
    const uint64_t byteLength = metadata.at("byteLength").get<uint64_t>();
    const uint64_t sampleCount = metadata.at("sampleCount").get<uint64_t>();
    const uint32_t checksum = metadata.at("crc32").get<uint32_t>();
    const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
    if (result.ChannelCount != descriptor.ChannelCount ||
        pixelCount > std::numeric_limits<uint64_t>::max() / result.ChannelCount ||
        sampleCount != pixelCount * result.ChannelCount ||
        sampleCount > std::numeric_limits<uint64_t>::max() / sizeof(float) ||
        byteLength != sampleCount * sizeof(float) || offset > payload.size() || byteLength > payload.size() - offset ||
        sampleCount > std::numeric_limits<size_t>::max() || byteLength > std::numeric_limits<size_t>::max()) {
      return std::unexpected(Fail("Analytical Frame plane dimensions or payload range are invalid"));
    }
    const auto bytes = payload.subspan(static_cast<size_t>(offset), static_cast<size_t>(byteLength));
    if (Crc32(bytes) != checksum) {
      return std::unexpected(Fail("Analytical Frame plane checksum mismatch"));
    }
    result.Pixels.resize(static_cast<size_t>(sampleCount));
    for (size_t index = 0; index < result.Pixels.size(); ++index) {
      result.Pixels[index] = std::bit_cast<float>(ReadU32(bytes, index * sizeof(uint32_t)));
      if (!std::isfinite(result.Pixels[index])) {
        return std::unexpected(Fail("Analytical Frame plane contains non-finite samples"));
      }
    }
  } catch (const Json::exception&) {
    return std::unexpected(Fail("Analytical Frame plane descriptor is incomplete or malformed"));
  }
  return result;
}

auto ParseVariant(const Json& value, std::span<const uint8_t> payload)
    -> Engine::Result<Editor::Panels::AnalyticalFrameVariant> {
  using namespace Engine::Renderer::BrdfAnalysis;
  if (!value.is_object()) {
    return std::unexpected(Fail("Analytical Frame variant must be an object"));
  }
  Editor::Panels::AnalyticalFrameVariant result;
  const auto model = ParseModel(value.value("model", Json{}));
  if (!model) {
    return std::unexpected(Fail("Analytical Frame uses an unsupported BRDF model"));
  }
  auto scene = DeserializeScene(value.value("requestScene", Json{}));
  if (!scene) {
    return std::unexpected(scene.error());
  }
  auto material = ParseMaterialProperties(value.value("material", Json{}));
  if (!material) {
    return std::unexpected(material.error());
  }
  auto camera = ParseCamera(value.value("camera", Json{}));
  if (!camera) {
    return std::unexpected(camera.error());
  }
  auto overrides = ParseOverrides(value.value("frameOverrides", Json{}));
  if (!overrides) {
    return std::unexpected(overrides.error());
  }
  auto curve = ParseCurve(value.value("curve", Json{}));
  if (!curve) {
    return std::unexpected(curve.error());
  }
  if (curve->Model != *model || material->Model != *model) {
    return std::unexpected(Fail("Analytical Frame variant model identifiers disagree"));
  }
  result.Curve = std::move(*curve);
  uint32_t width = 0;
  uint32_t height = 0;
  try {
    width = value.at("width").get<uint32_t>();
    height = value.at("height").get<uint32_t>();
    result.Render.Request.ShadowMapResolution = value.at("shadowMapResolution").get<uint32_t>();
    result.Render.Request.MaxShadowCastersPerScene = value.at("maxShadowCastersPerScene").get<uint32_t>();
    result.Render.Request.PostProcess = {};
    const Json& planes = value.at("planes");
    if (!planes.is_array() || planes.size() != 5) {
      return std::unexpected(Fail("Analytical Frame variant is missing captured AOV planes"));
    }
    auto& request = result.Render.Request;
    request.Scene = std::move(*scene);
    request.Model = *model;
    request.Material = *material;
    request.Camera = *camera;
    request.Width = width;
    request.Height = height;
    request.FrameOverrides = *overrides;
    if (width == 0 || height == 0) {
      return std::unexpected(Fail("Analytical Frame variant dimensions must be nonzero"));
    }
    std::vector<Engine::RenderOutputPlane> parsedPlanes;
    parsedPlanes.reserve(5);
    for (const auto& plane : planes) {
      auto parsed = ReadPlane(plane, payload, width, height);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      parsedPlanes.push_back(std::move(*parsed));
    }
    auto output = Engine::RenderOutput::CreateFromPlanes(width, height, std::move(parsedPlanes));
    if (!output) {
      return std::unexpected(Fail("Could not reconstruct Analytical Frame render output: " + output.error().Message));
    }
    result.Render.Output = std::move(*output);
    if (!ParseMeasurements(value.at("sceneLuminance"), result.SceneLuminance)) {
      return std::unexpected(Fail("Analytical Frame scene luminance measurements are invalid"));
    }
    if (width != request.Width || height != request.Height || width != result.Render.Output.Width ||
        height != result.Render.Output.Height || request.Model != result.Curve.Model) {
      return std::unexpected(Fail("Analytical Frame variant data is inconsistent"));
    }
  } catch (const Json::exception&) {
    return std::unexpected(Fail("Analytical Frame variant is incomplete or malformed"));
  }
  return result;
}

auto ValidatePayloadRanges(const Json& variants, uint64_t payloadSize) -> Engine::Status {
  std::vector<std::pair<uint64_t, uint64_t>> ranges;
  try {
    for (const auto& variant : variants) {
      for (const auto& plane : variant.at("planes")) {
        const uint64_t begin = plane.at("offset").get<uint64_t>();
        const uint64_t length = plane.at("byteLength").get<uint64_t>();
        if (begin > payloadSize || length > payloadSize - begin) {
          return std::unexpected(Fail("Analytical Frame plane payload range is out of bounds"));
        }
        ranges.emplace_back(begin, begin + length);
      }
    }
  } catch (const Json::exception&) {
    return std::unexpected(Fail("Analytical Frame payload index is incomplete or malformed"));
  }
  std::sort(ranges.begin(), ranges.end());
  uint64_t expectedOffset = 0;
  for (const auto& [begin, end] : ranges) {
    if (begin != expectedOffset || end < begin) {
      return std::unexpected(Fail("Analytical Frame payload contains overlapping or unreferenced ranges"));
    }
    expectedOffset = end;
  }
  if (expectedOffset != payloadSize) {
    return std::unexpected(Fail("Analytical Frame payload contains unreferenced data"));
  }
  return {};
}

auto ReadArchive(const std::filesystem::path& path) -> Engine::Result<Editor::Panels::AnalyticalFrameSession> {
  std::error_code error;
  const uintmax_t rawSize = std::filesystem::file_size(path, error);
  if (error || rawSize < HeaderBytes || rawSize > MaximumArchiveBytes || rawSize > std::numeric_limits<size_t>::max() ||
      rawSize > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
    return std::unexpected(Fail("Analytical Frame archive is missing, truncated, or too large"));
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::unexpected(Fail("Could not open Analytical Frame archive: " + path.string()));
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(rawSize));
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input || static_cast<size_t>(input.gcount()) != bytes.size()) {
    return std::unexpected(Fail("Could not read complete Analytical Frame archive"));
  }
  if (!std::equal(Magic.begin(), Magic.end(), bytes.begin())) {
    return std::unexpected(Fail("File is not a supported Analytical Frame archive"));
  }
  const uint64_t manifestSize = ReadU64(bytes, Magic.size());
  if (manifestSize == 0 || manifestSize > MaximumManifestBytes || manifestSize > bytes.size() - HeaderBytes) {
    return std::unexpected(Fail("Analytical Frame manifest size is invalid"));
  }
  Json manifest;
  try {
    manifest = Json::parse(bytes.begin() + static_cast<std::ptrdiff_t>(HeaderBytes),
                           bytes.begin() + static_cast<std::ptrdiff_t>(HeaderBytes + manifestSize));
  } catch (const Json::exception& exception) {
    return std::unexpected(Fail(std::string("Analytical Frame manifest is corrupt: ") + exception.what()));
  }
  if (!manifest.is_object()) {
    return std::unexpected(Fail("Analytical Frame manifest must be a JSON object"));
  }
  try {
    const uint32_t version = manifest.at("formatVersion").get<uint32_t>();
    if (version != FormatVersion) {
      return std::unexpected(Fail("Unsupported Analytical Frame archive version: " + std::to_string(version)));
    }
    const uint64_t payloadSize = manifest.at("payloadBytes").get<uint64_t>();
    if (payloadSize != bytes.size() - HeaderBytes - manifestSize) {
      return std::unexpected(Fail("Analytical Frame payload length does not match its manifest"));
    }
    const Json& variants = manifest.at("variants");
    if (!variants.is_array() || variants.empty() || variants.size() > 9U) {
      return std::unexpected(Fail("Analytical Frame archive contains no completed variants"));
    }
    const auto rangeStatus = ValidatePayloadRanges(variants, payloadSize);
    if (!rangeStatus) {
      return std::unexpected(rangeStatus.error());
    }
    Editor::Panels::AnalyticalFrameSession session;
    const auto snapshotStatus = ParseSnapshot(manifest.at("snapshot"), session);
    if (!snapshotStatus) {
      return std::unexpected(snapshotStatus.error());
    }
    session.TargetLabel = manifest.at("targetLabel").get<std::string>();
    session.ViewportState = manifest.at("viewportState");
    if (session.TargetLabel.empty()) {
      return std::unexpected(Fail("Analytical Frame target label is empty"));
    }
    const auto* payloadData = bytes.data() + static_cast<size_t>(HeaderBytes + manifestSize);
    const std::span<const uint8_t> payload(payloadData, static_cast<size_t>(payloadSize));
    std::vector<Engine::Renderer::BrdfModel> seenModels;
    session.Variants.reserve(variants.size());
    for (const auto& variantValue : variants) {
      auto variant = ParseVariant(variantValue, payload);
      if (!variant) {
        return std::unexpected(variant.error());
      }
      const auto model = variant->Render.Request.Model;
      if (std::find(seenModels.begin(), seenModels.end(), model) != seenModels.end()) {
        return std::unexpected(Fail("Analytical Frame archive contains duplicate model variants"));
      }
      seenModels.push_back(model);
      const auto requestStatus = ValidateVariantRequest(session.Snapshot, *variant);
      if (!requestStatus) {
        return std::unexpected(requestStatus.error());
      }
      if (variant->Render.Request.Width != variant->Render.Output.Width ||
          variant->Render.Request.Height != variant->Render.Output.Height) {
        return std::unexpected(Fail("Analytical Frame variant output dimensions do not match its request"));
      }
      if (!session.Variants.empty() &&
          (variant->Render.Output.Width != session.Variants.front().Render.Output.Width ||
           variant->Render.Output.Height != session.Variants.front().Render.Output.Height)) {
        return std::unexpected(Fail("Analytical Frame variants do not share the same output dimensions"));
      }
      session.Variants.push_back(std::move(*variant));
    }
    const auto viewportStatus = ViewportStateValid(session.ViewportState, session.Variants.front().Render.Output.Width,
                                                   session.Variants.front().Render.Output.Height);
    if (!viewportStatus) {
      return std::unexpected(viewportStatus.error());
    }
    return session;
  } catch (const Json::exception& exception) {
    return std::unexpected(Fail(std::string("Analytical Frame manifest is incomplete: ") + exception.what()));
  }
}

} // namespace

auto Editor::Panels::AnalyticalFrameArchive::Save(const std::filesystem::path& path,
                                                  const AnalyticalFrameSession& session) -> Engine::Status {
  if (path.empty() || path.extension() != ".aframe") {
    return std::unexpected(Fail("Analytical Frame destination must have a .aframe extension"));
  }
  try {
    std::vector<uint8_t> payload;
    auto manifest = BuildManifest(session, payload);
    if (!manifest) {
      return std::unexpected(manifest.error());
    }
    const std::string manifestText = manifest->dump();
    if (manifestText.empty() || manifestText.size() > MaximumManifestBytes) {
      return std::unexpected(Fail("Analytical Frame manifest exceeds the supported size"));
    }
    if (manifestText.size() > MaximumArchiveBytes - HeaderBytes ||
        payload.size() > MaximumArchiveBytes - HeaderBytes - manifestText.size()) {
      return std::unexpected(Fail("Analytical Frame archive exceeds the supported total size"));
    }
    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::path temporaryPath = path;
    temporaryPath += ".tmp-" + nonce;
    std::filesystem::path backupPath = path;
    backupPath += ".backup-" + nonce;
    {
      std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
      if (!output) {
        return std::unexpected(Fail("Could not open Analytical Frame destination: " + temporaryPath.string()));
      }
      output.write(reinterpret_cast<const char*>(Magic.data()), static_cast<std::streamsize>(Magic.size()));
      WriteU64(output, manifestText.size());
      output.write(manifestText.data(), static_cast<std::streamsize>(manifestText.size()));
      output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
      output.flush();
      if (!output) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(temporaryPath, ignored);
        return std::unexpected(Fail("Could not write complete Analytical Frame archive"));
      }
    }
    std::error_code error;
    const bool hadDestination = std::filesystem::exists(path, error);
    if (error) {
      const std::string message = error.message();
      std::error_code ignored;
      std::filesystem::remove(temporaryPath, ignored);
      return std::unexpected(Fail("Could not inspect Analytical Frame destination: " + message));
    }
    if (hadDestination && std::filesystem::is_directory(path, error)) {
      std::error_code ignored;
      std::filesystem::remove(temporaryPath, ignored);
      return std::unexpected(Fail("Analytical Frame destination is a directory"));
    }
    if (error) {
      std::error_code ignored;
      std::filesystem::remove(temporaryPath, ignored);
      return std::unexpected(Fail("Could not inspect Analytical Frame destination type: " + error.message()));
    }
    if (hadDestination) {
      std::filesystem::rename(path, backupPath, error);
      if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporaryPath, ignored);
        return std::unexpected(Fail("Could not preserve existing Analytical Frame destination: " + error.message()));
      }
    }
    std::filesystem::rename(temporaryPath, path, error);
    if (error) {
      std::error_code ignored;
      std::filesystem::remove(temporaryPath, ignored);
      if (hadDestination) {
        std::error_code restoreError;
        std::filesystem::rename(backupPath, path, restoreError);
        if (restoreError) {
          return std::unexpected(Fail("Could not replace Analytical Frame destination or restore its backup: " +
                                      error.message() + "; " + restoreError.message()));
        }
      }
      return std::unexpected(Fail("Could not replace Analytical Frame destination: " + error.message()));
    }
    if (hadDestination) {
      std::error_code ignored;
      std::filesystem::remove(backupPath, ignored);
    }
    return {};
  } catch (const std::exception& exception) {
    return std::unexpected(Fail(std::string("Could not serialize Analytical Frame archive: ") + exception.what()));
  } catch (...) {
    return std::unexpected(Fail("Could not serialize Analytical Frame archive due to an unknown error"));
  }
}

auto Editor::Panels::AnalyticalFrameArchive::Load(const std::filesystem::path& path)
    -> Engine::Result<AnalyticalFrameSession> {
  if (path.empty() || path.extension() != ".aframe") {
    return std::unexpected(Fail("Analytical Frame source must have a .aframe extension"));
  }
  try {
    return ReadArchive(path);
  } catch (const std::exception& exception) {
    return std::unexpected(Fail(std::string("Could not load Analytical Frame archive: ") + exception.what()));
  } catch (...) {
    return std::unexpected(Fail("Could not load Analytical Frame archive due to an unknown error"));
  }
}
