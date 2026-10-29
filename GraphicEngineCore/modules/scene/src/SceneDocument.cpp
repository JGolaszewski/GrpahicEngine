#include "scene/include/SceneDocument.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <set>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <Windows.h>
#endif

namespace Engine {
namespace {
using Json = nlohmann::json;

auto FileContext(const std::filesystem::path& path, const std::string& message) -> std::string {
  return path.generic_string() + ": " + message;
}

auto IsGuid(const std::string& value) -> bool {
  if (value.size() != 36) {
    return false;
  }
  for (size_t i = 0; i < value.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (value.at(i) != '-') {
        return false;
      }
    } else if (std::isxdigit(static_cast<unsigned char>(value.at(i))) == 0) {
      return false;
    }
  }
  return true;
}

template <size_t N>
auto ReadFloatArray(const Json& value, std::array<float, N>& result, const std::string& field, std::string& error)
    -> bool {
  if (!value.is_array() || value.size() != N) {
    error = field + " must be an array of " + std::to_string(N) + " numbers";
    return false;
  }
  for (size_t i = 0; i < N; ++i) {
    if (!value.at(i).is_number()) {
      error = field + " contains a non-numeric value";
      return false;
    }
    const double number = value.at(i).get<double>();
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) {
      error = field + " contains a non-finite or unrepresentable value";
      return false;
    }
    result.at(i) = static_cast<float>(number);
  }
  return true;
}

template <typename T> auto ReadNumber(const Json& object, const char* key, T& result, std::string& error) -> bool {
  if (!object.contains(key)) {
    return true;
  }
  const Json& value = object.at(key);
  if (!value.is_number()) {
    error = std::string(key) + " must be numeric";
    return false;
  }
  const double number = value.get<double>();
  if (!std::isfinite(number) || number < -static_cast<double>(std::numeric_limits<T>::max()) ||
      number > static_cast<double>(std::numeric_limits<T>::max())) {
    error = std::string(key) + " contains a non-finite or unrepresentable value";
    return false;
  }
  result = static_cast<T>(number);
  return true;
}

auto ReadBool(const Json& object, const char* key, bool& result, std::string& error) -> bool {
  if (!object.contains(key)) {
    return true;
  }
  if (!object.at(key).is_boolean()) {
    error = std::string(key) + " must be boolean";
    return false;
  }
  result = object.at(key).get<bool>();
  return true;
}

auto ReadId(const Json& object, uint64_t& id, std::string& error) -> bool {
  if (!object.contains("id") || !object.at("id").is_number_unsigned()) {
    error = "id must be an unsigned integer";
    return false;
  }
  id = object.at("id").get<uint64_t>();
  if (id == 0) {
    error = "id must be positive";
    return false;
  }
  return true;
}

auto ReadPositiveDimension(const Json& object, const char* key, uint32_t& result, std::string& error) -> bool {
  if (!object.contains(key)) {
    return true;
  }
  const Json& value = object.at(key);
  if (!value.is_number_unsigned() || value.get<uint64_t>() == 0 ||
      value.get<uint64_t>() > std::numeric_limits<uint32_t>::max()) {
    error = std::string(key) + " must be a positive 32-bit integer";
    return false;
  }
  result = value.get<uint32_t>();
  return true;
}

auto ReadGuid(const Json& object, const char* key, std::optional<SceneGuidDocument>& result, std::string& error)
    -> bool {
  if (!object.contains(key) || object.at(key).is_null()) {
    return true;
  }
  const Json& value = object.at(key);
  if (!value.is_object() || !value.contains("high") || !value.contains("low") ||
      !value.at("high").is_number_unsigned() || !value.at("low").is_number_unsigned()) {
    error = std::string(key) + " must be null or an object with unsigned high and low fields";
    return false;
  }
  result = SceneGuidDocument{.high = value.at("high").get<uint64_t>(), .low = value.at("low").get<uint64_t>()};
  return true;
}

template <size_t N>
auto ReadArrayField(const Json& object, const char* key, std::array<float, N>& result, std::string& error) -> bool {
  return !object.contains(key) || ReadFloatArray(object.at(key), result, key, error);
}

auto ReadOptionalGuid(const Json& object, const char* key, std::optional<std::string>& result, std::string& error)
    -> bool {
  if (!object.contains(key) || object.at(key).is_null()) {
    return true;
  }
  if (!object.at(key).is_string()) {
    error = std::string(key) + " must be a GUID string or null";
    return false;
  }
  result = object.at(key).get<std::string>();
  if (!IsGuid(*result)) {
    error = std::string(key) + " is not a valid GUID";
    return false;
  }
  return true;
}

auto KnownKeys(const Json& object, std::initializer_list<const char*> known, std::vector<std::string>& warnings,
               const std::string& context) -> bool;

auto ReadMaterialTextFields(const Json& value, SceneMaterialDocument& material, std::string& error) -> bool {
  const std::array fields = {
      std::pair{"brdfModel", &SceneMaterialDocument::brdfModel},
      std::pair{"workflow", &SceneMaterialDocument::workflow},
      std::pair{"specularMode", &SceneMaterialDocument::specularMode},
      std::pair{"alphaMode", &SceneMaterialDocument::alphaMode},
  };
  for (const auto& [key, member] : fields) {
    if (!value.contains(key)) {
      continue;
    }
    if (!value.at(key).is_string()) {
      error = std::string(key) + " must be a string";
      return false;
    }
    material.*member = value.at(key).get<std::string>();
  }
  return true;
}

auto ValidateMaterialNames(const Json& value, SceneMaterialDocument& material, std::vector<std::string>& warnings,
                           const std::string& context, std::string& error) -> bool {
  if (material.brdfModel == "legacy") {
    error = context + " uses the unsupported Legacy BRDF; reauthor this material with a supported BRDF distribution";
    return false;
  }
  if (material.brdfModel == "neural") {
    warnings.push_back(context + " uses the unsupported neural BRDF; GGX will be used instead");
    material.brdfModel = "ggx";
  }
  if (material.brdfModel == "specularGlossiness") {
    if (value.contains("workflow")) {
      error = "specularGlossiness is a workflow, not a brdfModel";
      return false;
    }
    material.brdfModel = "ggx";
    material.workflow = "specularGlossiness";
    warnings.push_back(context + " migrated specularGlossiness from brdfModel to workflow with GGX shading");
  }
  if (material.brdfModel != "ggx" && material.brdfModel != "gtr" && material.brdfModel != "beckmann" &&
      material.brdfModel != "gauss" && material.brdfModel != "blinnPhong" && material.brdfModel != "ashikhminShirley" &&
      material.brdfModel != "schlick" && material.brdfModel != "sawicki" && material.brdfModel != "prismNdf") {
    error = "brdfModel has an unsupported value";
    return false;
  }
  if (material.workflow != "metallicRoughness" && material.workflow != "specularGlossiness") {
    error = "workflow has an unsupported value";
    return false;
  }
  if (material.specularMode != "ior" && material.specularMode != "explicitF0") {
    error = "specularMode has an unsupported value";
    return false;
  }
  if (material.alphaMode != "opaque" && material.alphaMode != "mask" && material.alphaMode != "blend") {
    error = "alphaMode has an unsupported value";
    return false;
  }
  return true;
}

auto ReadGeneratedNdf(const Json& value, SceneGeneratedNdfDocument& generated, std::string& error) -> bool {
  if (!value.is_object()) {
    error = "generatedNdf must be an object";
    return false;
  }
  std::vector<std::string> warnings;
  KnownKeys(value,
            {"gridSize", "degree", "dataVersion", "directional", "prompt", "modelVersion", "exporterVersion",
             "normalization", "zernikeCoefficients", "residuals", "residualsEnabled", "residualFilter"},
            warnings, "generatedNdf");
  for (const char* required :
       {"gridSize", "degree", "dataVersion", "normalization", "zernikeCoefficients", "residuals"}) {
    if (!value.contains(required)) {
      error = std::string("generatedNdf is missing required field '") + required + "'";
      return false;
    }
  }
  const auto readUint = [&value, &error](const char* key, uint32_t& target) -> bool {
    if (!value.contains(key)) {
      return true;
    }
    const auto& field = value.at(key);
    if ((!field.is_number_unsigned() && !field.is_number_integer()) || field.get<int64_t>() < 0 ||
        field.get<uint64_t>() > std::numeric_limits<uint32_t>::max()) {
      error = std::string("generatedNdf.") + key + " must be a non-negative 32-bit integer";
      return false;
    }
    target = field.get<uint32_t>();
    return true;
  };
  if (!readUint("gridSize", generated.gridSize) || !readUint("degree", generated.degree) ||
      !readUint("dataVersion", generated.dataVersion) ||
      !ReadNumber(value, "normalization", generated.data.normalization, error) ||
      !ReadFloatArray(value.at("zernikeCoefficients"), generated.data.zernikeCoefficients,
                      "generatedNdf.zernikeCoefficients", error)) {
    return false;
  }
  const auto& residualJson = value.at("residuals");
  if (!residualJson.is_array()) {
    error = "generatedNdf.residuals must be an array";
    return false;
  }
  std::vector<float> serializedResiduals;
  serializedResiduals.reserve(residualJson.size());
  for (const auto& sample : residualJson) {
    if (!sample.is_number()) {
      error = "generatedNdf.residuals contains a non-numeric value";
      return false;
    }
    const double number = sample.get<double>();
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) {
      error = "generatedNdf.residuals contains a non-finite or unrepresentable value";
      return false;
    }
    serializedResiduals.push_back(static_cast<float>(number));
  }
  if (generated.gridSize == PrismNdfGridSize && generated.dataVersion == PrismNdfDataVersion &&
      serializedResiduals.size() == PrismNdfSampleCount) {
    generated.data.residuals = std::move(serializedResiduals);
  } else if (generated.gridSize == 64 && generated.dataVersion == 1 &&
             serializedResiduals.size() == size_t{64} * 64 && PrismNdfGridSize != 64) {
    // Schema-v9 assets stored the residual over a 64x64 grid. Preserve those
    // materials by bilinearly resampling it to the current canonical grid.
    constexpr uint32_t legacySize = 64;
    generated.data.residuals.resize(PrismNdfSampleCount);
    for (uint32_t y = 0; y < PrismNdfGridSize; ++y) {
      const float sourceY = static_cast<float>(y) * static_cast<float>(legacySize - 1) /
                            static_cast<float>(PrismNdfGridSize - 1);
      const uint32_t y0 = static_cast<uint32_t>(sourceY);
      const uint32_t y1 = std::min(y0 + 1, legacySize - 1);
      const float fy = sourceY - static_cast<float>(y0);
      for (uint32_t x = 0; x < PrismNdfGridSize; ++x) {
        const float sourceX = static_cast<float>(x) * static_cast<float>(legacySize - 1) /
                              static_cast<float>(PrismNdfGridSize - 1);
        const uint32_t x0 = static_cast<uint32_t>(sourceX);
        const uint32_t x1 = std::min(x0 + 1, legacySize - 1);
        const float fx = sourceX - static_cast<float>(x0);
        const auto at = [&serializedResiduals](uint32_t sx, uint32_t sy) {
          return serializedResiduals[static_cast<size_t>(sy) * legacySize + sx];
        };
        const float top = std::lerp(at(x0, y0), at(x1, y0), fx);
        const float bottom = std::lerp(at(x0, y1), at(x1, y1), fx);
        generated.data.residuals[static_cast<size_t>(y) * PrismNdfGridSize + x] = std::lerp(top, bottom, fy);
      }
    }
    generated.gridSize = PrismNdfGridSize;
    generated.dataVersion = PrismNdfDataVersion;
  } else {
    error = "generatedNdf has unsupported grid dimensions or residual sample count";
    return false;
  }
  const auto readString = [&value, &error](const char* key, std::string& target) -> bool {
    if (!value.contains(key)) {
      return true;
    }
    if (!value.at(key).is_string()) {
      error = std::string("generatedNdf.") + key + " must be a string";
      return false;
    }
    target = value.at(key).get<std::string>();
    return true;
  };
  if (!readString("prompt", generated.prompt) || !readString("modelVersion", generated.modelVersion) ||
      !readString("exporterVersion", generated.exporterVersion)) {
    return false;
  }
  if (value.contains("directional")) {
    if (!value.at("directional").is_boolean()) {
      error = "generatedNdf.directional must be a boolean";
      return false;
    }
    generated.directional = value.at("directional").get<bool>();
  }
  if (value.contains("residualsEnabled")) {
    if (!value.at("residualsEnabled").is_boolean()) {
      error = "generatedNdf.residualsEnabled must be a boolean";
      return false;
    }
    generated.data.residualsEnabled = value.at("residualsEnabled").get<bool>();
  }
  if (value.contains("residualFilter")) {
    if (!value.at("residualFilter").is_string()) {
      error = "generatedNdf.residualFilter must be a string";
      return false;
    }
    const auto filter = value.at("residualFilter").get<std::string>();
    if (filter == "quinticUv") generated.data.residualFilter = PrismNdfResidualFilter::QuinticUv;
    else if (filter != "bilinear") { error = "generatedNdf.residualFilter is unsupported"; return false; }
  }
  const bool finiteCoefficients =
      std::ranges::all_of(generated.data.zernikeCoefficients, [](float sample) { return std::isfinite(sample); });
  const bool finiteResiduals =
      std::ranges::all_of(generated.data.residuals, [](float sample) { return std::isfinite(sample); });
  if (generated.gridSize != PrismNdfGridSize || generated.data.residuals.size() != PrismNdfSampleCount ||
      generated.degree != PrismNdfDegree ||
      generated.dataVersion != PrismNdfDataVersion || !finiteCoefficients || !finiteResiduals ||
      !std::isfinite(generated.data.normalization) || generated.data.normalization <= 0.0F) {
    error = "generatedNdf has unsupported dimensions/version or non-finite data";
    return false;
  }
  return true;
}

auto ReadMaterial(const Json& value, SceneMaterialDocument& material, std::vector<std::string>& warnings,
                  const std::string& context, std::string& error) -> bool {
  if (!value.is_object()) {
    error = "material must be an object";
    return false;
  }
  KnownKeys(value,
            {
                "baseColor",
                "emissive",
                "metallic",
                "roughness",
                "gtrGamma",
                "anisotropicRoughnessU",
                "anisotropicRoughnessV",
                "ior",
                "specularF0",
                "brdfModel",
                "workflow",
                "specularMode",
                "alphaMode",
                "alphaCutoff",
                "doubleSided",
                "baseColorTextureGuid",
                "normalTextureGuid",
                "baseColorTexCoord",
                "normalTexCoord",
                "baseColorTextureOffset",
                "baseColorTextureScale",
                "baseColorTextureRotation",
                "normalTextureOffset",
                "normalTextureScale",
                "normalTextureRotation",
                "textureAnisotropyLevels",
                "normalScale",
                "generatedNdf",
            },
            warnings, context);
  if (!ReadArrayField(value, "baseColor", material.baseColor, error) ||
      !ReadArrayField(value, "emissive", material.emissive, error) ||
      !ReadNumber(value, "metallic", material.metallic, error) ||
      !ReadNumber(value, "roughness", material.roughness, error) ||
      !ReadNumber(value, "gtrGamma", material.gtrGamma, error) || !ReadNumber(value, "ior", material.ior, error) ||
      !ReadArrayField(value, "specularF0", material.specularF0, error) ||
      !ReadOptionalGuid(value, "baseColorTextureGuid", material.baseColorTextureGuid, error) ||
      !ReadOptionalGuid(value, "normalTextureGuid", material.normalTextureGuid, error) ||
      !ReadArrayField(value, "baseColorTextureOffset", material.baseColorTextureTransform.offset, error) ||
      !ReadArrayField(value, "baseColorTextureScale", material.baseColorTextureTransform.scale, error) ||
      !ReadNumber(value, "baseColorTextureRotation", material.baseColorTextureTransform.rotation, error) ||
      !ReadArrayField(value, "normalTextureOffset", material.normalTextureTransform.offset, error) ||
      !ReadArrayField(value, "normalTextureScale", material.normalTextureTransform.scale, error) ||
      !ReadNumber(value, "normalTextureRotation", material.normalTextureTransform.rotation, error)) {
    return false;
  }
  if (value.contains("normalScale") && !ReadNumber(value, "normalScale", material.normalScale, error)) {
    return false;
  }
  if (value.contains("textureAnisotropyLevels")) {
    const Json& levels = value.at("textureAnisotropyLevels");
    if (!levels.is_array() || levels.size() != material.textureAnisotropyLevels.size()) {
      error = "textureAnisotropyLevels must contain five integer values";
      return false;
    }
    for (size_t index = 0; index < levels.size(); ++index) {
      const Json& level = levels.at(index);
      if ((!level.is_number_unsigned() && !level.is_number_integer()) || level.get<int64_t>() < 0 ||
          level.get<int64_t>() > 16) {
        error = "textureAnisotropyLevels values must be non-negative integers";
        return false;
      }
      const auto parsed = static_cast<uint32_t>(level.get<int64_t>());
      if (parsed != 0 && parsed != 1 && parsed != 2 && parsed != 4 && parsed != 8 && parsed != 16) {
        error = "textureAnisotropyLevels values must be 0, 1, 2, 4, 8, or 16";
        return false;
      }
      material.textureAnisotropyLevels[index] = parsed;
    }
  }
  if (!std::isfinite(material.normalScale) || material.normalScale < 0.0F) {
    error = "normalScale must be a finite non-negative number";
    return false;
  }
  const auto readTexCoord = [&value, &error](const char* key, uint32_t& output) -> bool {
    if (!value.contains(key)) {
      return true;
    }
    if (!value.at(key).is_number_unsigned() && !value.at(key).is_number_integer()) {
      error = std::string(key) + " must be an integer";
      return false;
    }
    const auto coordinate = value.at(key).get<int64_t>();
    if (coordinate < 0 || coordinate >= 8) {
      error = std::string(key) + " must be in [0, 7]";
      return false;
    }
    output = static_cast<uint32_t>(coordinate);
    return true;
  };
  if (!readTexCoord("baseColorTexCoord", material.baseColorTexCoord) ||
      !readTexCoord("normalTexCoord", material.normalTexCoord)) {
    return false;
  }
  if (!ReadMaterialTextFields(value, material, error)) {
    return false;
  }
  if ((value.contains("anisotropicRoughnessU") &&
       !ReadNumber(value, "anisotropicRoughnessU", material.anisotropicRoughnessU, error)) ||
      (value.contains("anisotropicRoughnessV") &&
       !ReadNumber(value, "anisotropicRoughnessV", material.anisotropicRoughnessV, error)))
    return false;
  if (value.contains("alphaCutoff") && !ReadNumber(value, "alphaCutoff", material.alphaCutoff, error)) {
    return false;
  }
  if (value.contains("doubleSided")) {
    if (!value.at("doubleSided").is_boolean()) {
      error = "doubleSided must be a boolean";
      return false;
    }
    material.doubleSided = value.at("doubleSided").get<bool>();
  }
  if (!ValidateMaterialNames(value, material, warnings, context, error)) {
    return false;
  }
  if (value.contains("generatedNdf")) {
    SceneGeneratedNdfDocument generated;
    if (!ReadGeneratedNdf(value.at("generatedNdf"), generated, error)) {
      return false;
    }
    material.generatedNdf = std::move(generated);
  }
  if ((material.brdfModel == "prismNdf") != material.generatedNdf.has_value()) {
    error = "generatedNdf payload must be present exactly when brdfModel is prismNdf";
    return false;
  }
  if (!std::isfinite(material.alphaCutoff) || material.alphaCutoff < 0.0F || material.alphaCutoff > 1.0F) {
    error = "alphaCutoff must be within [0, 1]";
    return false;
  }
  if (!std::isfinite(material.anisotropicRoughnessU) || !std::isfinite(material.anisotropicRoughnessV)) {
    error = "anisotropic roughness values must be finite";
    return false;
  }
  const auto validTransform = [](const SceneMaterialTextureTransform& transform) -> bool {
    return std::ranges::all_of(transform.offset, [](float value) { return std::isfinite(value); }) &&
           std::ranges::all_of(transform.scale, [](float value) { return std::isfinite(value); }) &&
           std::isfinite(transform.rotation);
  };
  if (!validTransform(material.baseColorTextureTransform) || !validTransform(material.normalTextureTransform)) {
    error = "material texture transforms must contain finite values";
    return false;
  }
  if (material.metallic < 0.0F || material.metallic > 1.0F || material.roughness < 0.0F || material.roughness > 1.0F ||
      material.gtrGamma < 1.0F || material.gtrGamma > 8.0F || material.ior < 1.0F || material.ior > 3.0F ||
      material.anisotropicRoughnessU < 0.0F || material.anisotropicRoughnessU > 1.0F ||
      material.anisotropicRoughnessV < 0.0F || material.anisotropicRoughnessV > 1.0F) {
    error = "material contains out-of-range BRDF settings";
    return false;
  }
  for (float component : material.baseColor) {
    if (component < 0.0F || component > 1.0F) {
      error = "baseColor values must be within [0, 1]";
      return false;
    }
  }
  for (float component : material.emissive) {
    if (component < 0.0F) {
      error = "emissive values must be non-negative";
      return false;
    }
  }
  for (float component : material.specularF0) {
    if (component < 0.0F || component > 1.0F) {
      error = "specularF0 values must be within [0, 1]";
      return false;
    }
  }
  return true;
}

auto IsValidPrimitiveType(const std::string& type) -> bool {
  return type == "cube" || type == "sphere" || type == "plane" || type == "cone";
}

auto IsValidMaterial(const SceneMaterialDocument& material) -> bool {
  const auto validBrdf =
      material.brdfModel == "ggx" || material.brdfModel == "gtr" || material.brdfModel == "beckmann" ||
      material.brdfModel == "gauss" || material.brdfModel == "blinnPhong" || material.brdfModel == "ashikhminShirley" ||
      material.brdfModel == "schlick" || material.brdfModel == "sawicki" || material.brdfModel == "prismNdf";
  const bool validGeneratedNdf =
      material.generatedNdf.has_value() == (material.brdfModel == "prismNdf") &&
      (!material.generatedNdf ||
       (material.generatedNdf->gridSize == PrismNdfGridSize &&
        material.generatedNdf->data.residuals.size() == PrismNdfSampleCount &&
        material.generatedNdf->degree == PrismNdfDegree &&
        material.generatedNdf->dataVersion == PrismNdfDataVersion &&
        std::isfinite(material.generatedNdf->data.normalization) && material.generatedNdf->data.normalization > 0.0F &&
        std::ranges::all_of(material.generatedNdf->data.zernikeCoefficients,
                            [](float value) { return std::isfinite(value); }) &&
        std::ranges::all_of(material.generatedNdf->data.residuals, [](float value) { return std::isfinite(value); })));
  const bool validWorkflow = material.workflow == "metallicRoughness" || material.workflow == "specularGlossiness";
  if (!validBrdf || !validGeneratedNdf || !validWorkflow ||
      (material.specularMode != "ior" && material.specularMode != "explicitF0") ||
      (material.alphaMode != "opaque" && material.alphaMode != "mask" && material.alphaMode != "blend") ||
      !std::isfinite(material.metallic) || !std::isfinite(material.roughness) || !std::isfinite(material.gtrGamma) ||
      !std::isfinite(material.ior) || !std::isfinite(material.alphaCutoff) || !std::isfinite(material.normalScale) ||
      material.normalScale < 0.0F || !std::isfinite(material.anisotropicRoughnessU) ||
      !std::isfinite(material.anisotropicRoughnessV) || !std::isfinite(material.baseColorTextureTransform.rotation) ||
      !std::isfinite(material.normalTextureTransform.rotation) || material.baseColorTexCoord >= 8 ||
      material.normalTexCoord >= 8 || material.metallic < 0.0F || material.metallic > 1.0F ||
      material.roughness < 0.0F || material.roughness > 1.0F || material.gtrGamma < 1.0F || material.gtrGamma > 8.0F ||
      material.ior < 1.0F || material.ior > 3.0F || material.alphaCutoff < 0.0F || material.alphaCutoff > 1.0F ||
      material.anisotropicRoughnessU < 0.0F || material.anisotropicRoughnessU > 1.0F ||
      material.anisotropicRoughnessV < 0.0F || material.anisotropicRoughnessV > 1.0F) {
    return false;
  }
  if (!std::ranges::all_of(material.textureAnisotropyLevels, [](uint32_t value) {
        return value == 0 || value == 1 || value == 2 || value == 4 || value == 8 || value == 16;
      })) {
    return false;
  }
  if ((material.baseColorTextureGuid && !IsGuid(*material.baseColorTextureGuid)) ||
      (material.normalTextureGuid && !IsGuid(*material.normalTextureGuid))) {
    return false;
  }
  for (const auto* transform : {&material.baseColorTextureTransform, &material.normalTextureTransform}) {
    if (!std::ranges::all_of(transform->offset, [](float value) { return std::isfinite(value); }) ||
        !std::ranges::all_of(transform->scale, [](float value) { return std::isfinite(value); })) {
      return false;
    }
  }
  for (float value : material.baseColor) {
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
      return false;
    }
  }
  for (float value : material.emissive) {
    if (!std::isfinite(value) || value < 0.0F) {
      return false;
    }
  }
  return std::ranges::all_of(
      material.specularF0, [](float value) -> bool { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; });
}

auto KnownKeys(const Json& object, std::initializer_list<const char*> known, std::vector<std::string>& warnings,
               const std::string& context) -> bool {
  if (!object.is_object()) {
    return true;
  }
  for (auto it = object.begin(); it != object.end(); ++it) {
    const bool found = std::ranges::any_of(known, [&](const char* key) -> bool { return it.key() == key; });
    if (!found) {
      warnings.push_back(context + " contains unknown field '" + it.key() + "' (ignored)");
    }
  }
  return true;
}

auto MaterialJson(const SceneMaterialDocument& material) -> Json {
  Json result = {
      {"baseColor", material.baseColor},
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
      {"baseColorTextureGuid", material.baseColorTextureGuid ? Json(*material.baseColorTextureGuid) : Json(nullptr)},
      {"normalTextureGuid", material.normalTextureGuid ? Json(*material.normalTextureGuid) : Json(nullptr)},
      {"baseColorTexCoord", material.baseColorTexCoord},
      {"normalTexCoord", material.normalTexCoord},
      {"baseColorTextureOffset", material.baseColorTextureTransform.offset},
      {"baseColorTextureScale", material.baseColorTextureTransform.scale},
      {"baseColorTextureRotation", material.baseColorTextureTransform.rotation},
      {"normalTextureOffset", material.normalTextureTransform.offset},
      {"normalTextureScale", material.normalTextureTransform.scale},
      {"normalTextureRotation", material.normalTextureTransform.rotation},
      {"textureAnisotropyLevels", material.textureAnisotropyLevels},
      {"normalScale", material.normalScale},
  };
  if (material.generatedNdf) {
    const auto& generated = *material.generatedNdf;
    result["generatedNdf"] = {{"gridSize", generated.gridSize},
                              {"degree", generated.degree},
                              {"dataVersion", generated.dataVersion},
                              {"directional", generated.directional},
                              {"prompt", generated.prompt},
                              {"modelVersion", generated.modelVersion},
                              {"exporterVersion", generated.exporterVersion},
                              {"normalization", generated.data.normalization},
                              {"zernikeCoefficients", generated.data.zernikeCoefficients},
                              {"residuals", generated.data.residuals},
                              {"residualsEnabled", generated.data.residualsEnabled},
                              {"residualFilter", generated.data.residualFilter == PrismNdfResidualFilter::QuinticUv ? "quinticUv" : "bilinear"}};
  }
  return result;
}

auto GuidJson(const std::optional<SceneGuidDocument>& guid) -> Json {
  if (!guid) {
    return nullptr;
  }
  return Json{{"high", guid->high}, {"low", guid->low}};
}

auto HasMeshRenderer(const SceneEntityDocument& entity) -> bool {
  return entity.hasMeshRenderer || entity.modelGuid.has_value() || entity.materialGuid.has_value() ||
         entity.primitiveType.has_value() || entity.material.has_value() || !entity.materialOverrides.empty() ||
         entity.forceWireframe || !entity.meshVisible || entity.disableDepthTest || !entity.castShadows ||
         !entity.receiveShadows;
}

auto EntityJson(const SceneEntityDocument& entity) -> Json {
  Json item = Json::object();
  item.emplace("id", entity.id);
  item.emplace("name", entity.name);
  item.emplace("sceneRoot", entity.sceneRoot);
  item.emplace("identityGuid", GuidJson(entity.identityGuid));
  item.emplace("parentGuid", GuidJson(entity.parentGuid));
  item.emplace("modelGuid", entity.modelGuid ? Json(*entity.modelGuid) : Json(nullptr));
  item.emplace("materialGuid", entity.materialGuid ? Json(*entity.materialGuid) : Json(nullptr));
  item.emplace("primitiveType", entity.primitiveType ? Json(*entity.primitiveType) : Json(nullptr));
  item.emplace("hasMeshRenderer", !entity.sceneRoot && HasMeshRenderer(entity));
  if (!entity.sceneRoot) {
    item.emplace("visible", entity.visible);
    item.emplace("position", entity.position);
    item.emplace("rotation", entity.rotation);
    item.emplace("scale", entity.scale);
    item.emplace("flipHorizontal", entity.flipHorizontal);
    item.emplace("flipVertical", entity.flipVertical);
    Json render = Json::object();
    render.emplace("visible", entity.meshVisible);
    render.emplace("forceWireframe", entity.forceWireframe);
    render.emplace("disableDepthTest", entity.disableDepthTest);
    render.emplace("castShadows", entity.castShadows);
    render.emplace("receiveShadows", entity.receiveShadows);
    item.emplace("render", std::move(render));
  }
  if (entity.material) {
    item.emplace("material", MaterialJson(*entity.material));
  }
  if (!entity.materialOverrides.empty()) {
    Json overrides = Json::array();
    for (const auto& material : entity.materialOverrides) {
      overrides.push_back(material ? MaterialJson(*material) : Json(nullptr));
    }
    item.emplace("materialOverrides", std::move(overrides));
  }
  return item;
}

auto LightJson(const SceneLightDocument& light) -> Json {
  return {
      {"id", light.id},
      {"name", light.name},
      {"identityGuid", GuidJson(light.identityGuid)},
      {"parentGuid", GuidJson(light.parentGuid)},
      {"visible", light.visible},
      {"direction", light.direction},
      {"position", light.position},
      {"rotation", light.rotation},
      {"scale", light.scale},
      {"flipHorizontal", light.flipHorizontal},
      {"flipVertical", light.flipVertical},
      {"color", light.color},
      {"intensity", light.intensity},
      {"range", light.range},
      {"innerAngleDegrees", light.innerAngleDegrees},
      {"outerAngleDegrees", light.outerAngleDegrees},
      {"shadowBias", light.shadowBias},
      {"shadowNormalBias", light.shadowNormalBias},
      {"type", light.type},
      {"usePosition", light.usePosition},
      {"castShadows", light.castShadows},
  };
}

auto CameraPostProcessingJson(const Ecs::CameraPostProcessing& postProcessing) -> Json {
  Json result = Json::array();
  for (const auto& effect : postProcessing.Effects) {
    if (const auto* grading = std::get_if<Ecs::ColorGradingEffect>(&effect)) {
      result.push_back({{"type", "colorGrading"},
                        {"exposure", grading->Exposure},
                        {"contrast", grading->Contrast},
                        {"saturation", grading->Saturation}});
    } else if (const auto* bloom = std::get_if<Ecs::BloomEffect>(&effect)) {
      result.push_back({{"type", "bloom"},
                        {"threshold", bloom->Threshold},
                        {"intensity", bloom->Intensity},
                        {"radius", bloom->Radius},
                        {"smooth", bloom->Smooth}});
    } else if (const auto* fxaa = std::get_if<Ecs::FxaaEffect>(&effect)) {
      const char* quality = fxaa->Quality == Ecs::FxaaQuality::Low    ? "low"
                            : fxaa->Quality == Ecs::FxaaQuality::High ? "high"
                                                                      : "medium";
      result.push_back({{"type", "fxaa"}, {"quality", quality}, {"edgeThreshold", fxaa->EdgeThreshold}});
    }
  }
  return result;
}

auto ReadCameraPostProcessing(const Json& value, Ecs::CameraPostProcessing& postProcessing, std::string& error)
    -> bool {
  if (!value.is_array()) {
    error = "postProcessing must be an array";
    return false;
  }
  for (size_t index = 0; index < value.size(); ++index) {
    const Json& effect = value.at(index);
    if (!effect.is_object() || !effect.contains("type") || !effect.at("type").is_string()) {
      error = "postProcessing[" + std::to_string(index) + "] must have a string type";
      return false;
    }
    const std::string type = effect.at("type").get<std::string>();
    if (type == "colorGrading") {
      Ecs::ColorGradingEffect grading;
      if (!ReadNumber(effect, "exposure", grading.Exposure, error) ||
          !ReadNumber(effect, "contrast", grading.Contrast, error) ||
          !ReadNumber(effect, "saturation", grading.Saturation, error)) {
        return false;
      }
      postProcessing.Effects.emplace_back(grading);
    } else if (type == "bloom") {
      Ecs::BloomEffect bloom;
      if (!ReadNumber(effect, "threshold", bloom.Threshold, error) ||
          !ReadNumber(effect, "intensity", bloom.Intensity, error) ||
          !ReadNumber(effect, "radius", bloom.Radius, error)) {
        return false;
      }
      if (effect.contains("smooth") && !ReadBool(effect, "smooth", bloom.Smooth, error)) {
        return false;
      }
      postProcessing.Effects.emplace_back(bloom);
    } else if (type == "fxaa") {
      Ecs::FxaaEffect fxaa;
      if (effect.contains("quality")) {
        if (!effect.at("quality").is_string()) {
          error = "FXAA quality must be a string";
          return false;
        }
        const std::string quality = effect.at("quality").get<std::string>();
        if (quality == "low") {
          fxaa.Quality = Ecs::FxaaQuality::Low;
        } else if (quality == "medium") {
          fxaa.Quality = Ecs::FxaaQuality::Medium;
        } else if (quality == "high") {
          fxaa.Quality = Ecs::FxaaQuality::High;
        } else {
          error = "FXAA quality must be low, medium, or high";
          return false;
        }
      }
      if (effect.contains("edgeThreshold") && !ReadNumber(effect, "edgeThreshold", fxaa.EdgeThreshold, error)) {
        return false;
      }
      postProcessing.Effects.emplace_back(fxaa);
    } else {
      error = "postProcessing[" + std::to_string(index) + "] has an unsupported effect type";
      return false;
    }
  }
  const Status valid = Ecs::Validate(postProcessing);
  if (!valid) {
    error = valid.error().Message;
    return false;
  }
  return true;
}

auto CameraAmbientOcclusionJson(const Ecs::CameraPostProcessing* postProcessing) -> Json {
  if (postProcessing == nullptr || !postProcessing->AmbientOcclusion) {
    return nullptr;
  }
  return {{"radius", postProcessing->AmbientOcclusion->Radius},
          {"intensity", postProcessing->AmbientOcclusion->Intensity},
          {"sampleCount", postProcessing->AmbientOcclusion->SampleCount},
          {"depthBias", postProcessing->AmbientOcclusion->DepthBias}};
}

auto ReadCameraAmbientOcclusion(const Json& value, Ecs::CameraPostProcessing& postProcessing, std::string& error)
    -> bool {
  if (value.is_null()) {
    return true;
  }
  if (!value.is_object()) {
    error = "ambientOcclusion must be an object or null";
    return false;
  }
  Ecs::ScreenSpaceAmbientOcclusionSettings settings;
  if (!ReadNumber(value, "radius", settings.Radius, error) ||
      !ReadNumber(value, "intensity", settings.Intensity, error)) {
    return false;
  }
  if (value.contains("sampleCount")) {
    if (!value.at("sampleCount").is_number_unsigned()) {
      error = "ambientOcclusion.sampleCount must be an unsigned integer";
      return false;
    }
    settings.SampleCount = value.at("sampleCount").get<uint32_t>();
  }
  if (value.contains("depthBias") && !ReadNumber(value, "depthBias", settings.DepthBias, error)) {
    return false;
  }
  postProcessing.AmbientOcclusion = settings;
  const Status valid = Ecs::Validate(postProcessing);
  if (!valid) {
    error = valid.error().Message;
    return false;
  }
  return true;
}

auto AmbientLightJson(const AmbientLightSettings& settings) -> Json {
  return {{"enabled", settings.Enabled}, {"color", settings.Color}, {"intensity", settings.Intensity}};
}

auto ReadAmbientLight(const Json& value, AmbientLightSettings& settings, std::string& error) -> bool {
  if (!value.is_object() || !value.contains("enabled") || !value.contains("color") || !value.contains("intensity")) {
    error = "ambientLight must contain enabled, color, and intensity fields";
    return false;
  }
  if (!ReadBool(value, "enabled", settings.Enabled, error) ||
      !ReadFloatArray(value.at("color"), settings.Color, "color", error) ||
      !ReadNumber(value, "intensity", settings.Intensity, error)) {
    return false;
  }
  const Status valid = Engine::Validate(settings);
  if (!valid) {
    error = valid.error().Message;
    return false;
  }
  return true;
}

auto SkyboxJson(const SceneSkyboxDocument& skybox) -> Json {
  return {{"mode", skybox.mode},
          {"environmentGuid", skybox.environmentGuid},
          {"textureGuid", skybox.textureGuid},
          {"color", skybox.color},
          {"enableEnvironmentLighting", skybox.enableEnvironmentLighting},
          {"intensity", skybox.intensity}};
}

auto ReadSkybox(const Json& value, SceneSkyboxDocument& skybox, std::vector<std::string>& warnings, std::string& error)
    -> bool {
  if (!value.is_object()) {
    error = "skybox must be an object";
    return false;
  }
  KnownKeys(value, {"mode", "environmentGuid", "textureGuid", "color", "enableEnvironmentLighting", "intensity"},
            warnings, "skybox");
  if (value.contains("mode")) {
    if (!value.at("mode").is_string()) {
      error = "mode must be a string";
      return false;
    }
    skybox.mode = value.at("mode").get<std::string>();
    if (skybox.mode != "hdr" && skybox.mode != "ldr" && skybox.mode != "color") {
      error = "mode must be 'hdr', 'ldr', or 'color'";
      return false;
    }
  }
  if (value.contains("environmentGuid")) {
    if (!value.at("environmentGuid").is_string()) {
      error = "environmentGuid must be a string";
      return false;
    }
    skybox.environmentGuid = value.at("environmentGuid").get<std::string>();
    if (!skybox.environmentGuid.empty() && !IsGuid(skybox.environmentGuid)) {
      error = "environmentGuid must be empty or a valid asset GUID";
      return false;
    }
  }
  if (value.contains("textureGuid")) {
    if (!value.at("textureGuid").is_string()) {
      error = "textureGuid must be a string";
      return false;
    }
    skybox.textureGuid = value.at("textureGuid").get<std::string>();
    if (!skybox.textureGuid.empty() && !IsGuid(skybox.textureGuid)) {
      error = "textureGuid must be empty or a valid asset GUID";
      return false;
    }
  }
  if (value.contains("color") && !ReadFloatArray(value.at("color"), skybox.color, "color", error)) {
    return false;
  }
  if (!ReadBool(value, "enableEnvironmentLighting", skybox.enableEnvironmentLighting, error) ||
      !ReadNumber(value, "intensity", skybox.intensity, error)) {
    return false;
  }
  if (!std::isfinite(skybox.intensity) || skybox.intensity < 0.0F) {
    error = "skybox intensity must be finite and nonnegative";
    return false;
  }
  return true;
}

auto CameraJson(const SceneCameraDocument& camera) -> Json {
  return {
      {"id", camera.id},
      {"name", camera.name},
      {"identityGuid", GuidJson(camera.identityGuid)},
      {"parentGuid", GuidJson(camera.parentGuid)},
      {"visible", camera.visible},
      {"position", camera.position},
      {"rotation", camera.rotation},
      {"scale", camera.scale},
      {"flipHorizontal", camera.flipHorizontal},
      {"flipVertical", camera.flipVertical},
      {"fovRadians", camera.fovRadians},
      {"nearPlane", camera.nearPlane},
      {"farPlane", camera.farPlane},
      {"outputWidth", camera.outputWidth},
      {"outputHeight", camera.outputHeight},
      {"priority", camera.priority},
      {"active", camera.active},
      {"ambientLightOverride",
       camera.ambientLightOverride ? AmbientLightJson(*camera.ambientLightOverride) : Json(nullptr)},
      {"postProcessing", camera.postProcessing ? CameraPostProcessingJson(*camera.postProcessing) : Json(nullptr)},
      {"ambientOcclusion", CameraAmbientOcclusionJson(camera.postProcessing ? &*camera.postProcessing : nullptr)},
  };
}

auto ToJson(const SceneDocument& document) -> Json {
  Json result = {
      {"schemaVersion", document.schemaVersion},
      {"unitsPerMeter", document.unitsPerMeter},
      {"ambientLight", AmbientLightJson(document.ambientLight)},
      {"skybox", document.skybox ? SkyboxJson(*document.skybox) : Json(nullptr)},
      {"coordinateSystem", "right-handed-y-up"},
      {"entities", Json::array()},
      {"lights", Json::array()},
      {"cameras", Json::array()},
  };
  for (const auto& entity : document.entities) {
    result.at("entities").push_back(EntityJson(entity));
  }
  for (const auto& light : document.lights) {
    result.at("lights").push_back(LightJson(light));
  }
  for (const auto& camera : document.cameras) {
    result.at("cameras").push_back(CameraJson(camera));
  }
  return result;
}

auto ValidateSceneHeader(const std::filesystem::path& path, const SceneDocument& document, std::string& error) -> bool {
  if ((document.schemaVersion >= 1 && document.schemaVersion <= CurrentSceneSchemaVersion) &&
      std::isfinite(document.unitsPerMeter) && document.unitsPerMeter > 0.0F) {
    if (document.skybox &&
        ((document.skybox->mode != "hdr" && document.skybox->mode != "ldr" && document.skybox->mode != "color") ||
         (!document.skybox->environmentGuid.empty() && !IsGuid(document.skybox->environmentGuid)) ||
         (!document.skybox->textureGuid.empty() && !IsGuid(document.skybox->textureGuid)) ||
         !std::isfinite(document.skybox->intensity) || document.skybox->intensity < 0.0F ||
         std::ranges::any_of(
             document.skybox->color, [](float value) { return !std::isfinite(value) || value < 0.0F; }))) {
      error = FileContext(path, "document contains invalid skybox settings or environment GUID");
      return false;
    }
    if (const Status ambientLightValidation = Engine::Validate(document.ambientLight); ambientLightValidation) {
      return true;
    } else {
      error = FileContext(path, ambientLightValidation.error().Message);
      return false;
    }
  }
  error = FileContext(path, "document has an unsupported version or invalid unitsPerMeter");
  return false;
}

auto ValidateEntity(const SceneEntityDocument& entity, std::set<uint64_t>& ids, std::string& error) -> bool {
  if (entity.id == 0 || !ids.insert(entity.id).second || (entity.modelGuid && !IsGuid(*entity.modelGuid)) ||
      (entity.materialGuid && !IsGuid(*entity.materialGuid))) {
    error = "document contains a duplicate/invalid entity ID or asset GUID";
    return false;
  }
  if (entity.sceneRoot && (entity.hasMeshRenderer || entity.modelGuid || entity.primitiveType || entity.materialGuid ||
                           entity.material || entity.parentGuid || !entity.materialOverrides.empty())) {
    error = "scene root cannot contain mesh or material data";
    return false;
  }
  if ((entity.modelGuid && entity.primitiveType) ||
      (entity.primitiveType && !IsValidPrimitiveType(*entity.primitiveType)) ||
      (entity.material && !IsValidMaterial(*entity.material)) ||
      (!entity.materialOverrides.empty() && !entity.modelGuid)) {
    error = "document contains an invalid entity asset descriptor or material";
    return false;
  }
  if (std::ranges::any_of(entity.materialOverrides,
                          [](const auto& material) -> auto { return material && !IsValidMaterial(*material); })) {
    error = "document contains an invalid model material override";
    return false;
  }
  if (!entity.sceneRoot) {
    const std::array<float, 10> values = {
        entity.position.at(0), entity.position.at(1), entity.position.at(2), entity.rotation.at(0),
        entity.rotation.at(1), entity.rotation.at(2), entity.rotation.at(3), entity.scale.at(0),
        entity.scale.at(1),    entity.scale.at(2),
    };
    const float rotationLengthSquared =
        (entity.rotation.at(0) * entity.rotation.at(0)) + (entity.rotation.at(1) * entity.rotation.at(1)) +
        (entity.rotation.at(2) * entity.rotation.at(2)) + (entity.rotation.at(3) * entity.rotation.at(3));
    if (!std::ranges::all_of(values, [](float value) -> bool { return std::isfinite(value); }) ||
        !std::isfinite(rotationLengthSquared) || rotationLengthSquared <= 1.0e-12F) {
      error = "document contains an invalid entity transform";
      return false;
    }
  }
  return true;
}

auto ValidateEntities(const std::filesystem::path& path, const SceneDocument& document, std::string& error) -> bool {
  std::set<uint64_t> ids;
  size_t sceneRootCount = 0;
  for (const auto& entity : document.entities) {
    sceneRootCount += entity.sceneRoot ? 1U : 0U;
    if (!ValidateEntity(entity, ids, error)) {
      error = FileContext(path, error);
      return false;
    }
  }
  if (sceneRootCount > 1) {
    error = FileContext(path, "document contains multiple scene roots");
    return false;
  }
  return true;
}

auto ValidateLight(const SceneLightDocument& light, std::set<uint64_t>& ids) -> bool {
  const bool validType = light.type == "directional" || light.type == "point" || light.type == "spot";
  const std::array<float, 14> commonValues = {
      light.direction.at(0), light.direction.at(1),  light.direction.at(2), light.position.at(0), light.position.at(1),
      light.position.at(2),  light.color.at(0),      light.color.at(1),     light.color.at(2),    light.intensity,
      light.shadowBias,      light.shadowNormalBias, light.rotation.at(0),  light.rotation.at(1),
  };
  if (light.id == 0 || !ids.insert(light.id).second || !validType ||
      !std::ranges::all_of(commonValues, [](float value) -> bool { return std::isfinite(value); }) ||
      light.intensity < 0.0F ||
      ((light.type == "point" || light.type == "spot") && (!std::isfinite(light.range) || light.range <= 0.0F)) ||
      (light.type == "spot" && (!std::isfinite(light.innerAngleDegrees) || !std::isfinite(light.outerAngleDegrees) ||
                                light.innerAngleDegrees < 0.0F || light.outerAngleDegrees <= light.innerAngleDegrees ||
                                light.outerAngleDegrees > 89.5F)) ||
      light.shadowBias < 0.0F || light.shadowNormalBias < 0.0F) {
    return false;
  }
  const float directionLengthSquared = (light.direction.at(0) * light.direction.at(0)) +
                                       (light.direction.at(1) * light.direction.at(1)) +
                                       (light.direction.at(2) * light.direction.at(2));
  const float rotationLengthSquared =
      (light.rotation.at(0) * light.rotation.at(0)) + (light.rotation.at(1) * light.rotation.at(1)) +
      (light.rotation.at(2) * light.rotation.at(2)) + (light.rotation.at(3) * light.rotation.at(3));
  const bool validDirection =
      std::isfinite(directionLengthSquared) && (light.type == "point" || directionLengthSquared > 1.0e-12F);
  return validDirection && std::isfinite(rotationLengthSquared) && rotationLengthSquared > 1.0e-12F &&
         std::ranges::all_of(light.scale, [](float value) { return std::isfinite(value) && value > 0.0F; }) &&
         light.color.at(0) >= 0.0F && light.color.at(1) >= 0.0F && light.color.at(2) >= 0.0F;
}

auto ValidateLights(const std::filesystem::path& path, const SceneDocument& document, std::string& error) -> bool {
  std::set<uint64_t> ids;
  for (const auto& light : document.lights) {
    if (!ValidateLight(light, ids)) {
      error = FileContext(path, "document contains invalid light data");
      return false;
    }
  }
  return true;
}

auto ValidateCamera(const SceneCameraDocument& camera, std::set<uint64_t>& ids) -> bool {
  const std::array<float, 10> values = {
      camera.position.at(0), camera.position.at(1), camera.position.at(2), camera.rotation.at(0), camera.rotation.at(1),
      camera.rotation.at(2), camera.rotation.at(3), camera.fovRadians,     camera.nearPlane,      camera.farPlane,
  };
  const float rotationLengthSquared =
      (camera.rotation.at(0) * camera.rotation.at(0)) + (camera.rotation.at(1) * camera.rotation.at(1)) +
      (camera.rotation.at(2) * camera.rotation.at(2)) + (camera.rotation.at(3) * camera.rotation.at(3));
  return camera.id != 0 && ids.insert(camera.id).second &&
         std::ranges::all_of(values, [](float value) -> bool { return std::isfinite(value); }) &&
         camera.fovRadians > 0.0F && camera.fovRadians < std::numbers::pi_v<float> && camera.nearPlane > 0.0F &&
         camera.farPlane > camera.nearPlane && camera.outputWidth > 0 && camera.outputHeight > 0 &&
         std::isfinite(rotationLengthSquared) && rotationLengthSquared > 1.0e-12F &&
         std::ranges::all_of(camera.scale, [](float value) { return std::isfinite(value) && value > 0.0F; }) &&
         (!camera.ambientLightOverride || Engine::Validate(*camera.ambientLightOverride).has_value()) &&
         (!camera.postProcessing || Ecs::Validate(*camera.postProcessing).has_value());
}

auto ValidateCameras(const std::filesystem::path& path, const SceneDocument& document, std::string& error) -> bool {
  std::set<uint64_t> ids;
  for (const auto& camera : document.cameras) {
    if (!ValidateCamera(camera, ids)) {
      error = FileContext(path, "document contains invalid camera data");
      return false;
    }
  }
  return true;
}

auto WriteSceneAtomically(const std::filesystem::path& path, const std::filesystem::path& temporaryPath,
                          const SceneDocument& document, std::string& error) -> bool {
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  {
    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = FileContext(path, "could not create temporary scene file");
      return false;
    }
    output << ToJson(document).dump(2) << '\n';
    output.flush();
    if (!output) {
      error = FileContext(path, "could not write temporary scene file");
      return false;
    }
  }
#ifdef _WIN32
  if (MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
    const DWORD code = GetLastError();
    error = FileContext(path, "atomic replacement failed with Windows error " + std::to_string(code));
    return false;
  }
#else
  std::filesystem::rename(temporaryPath, path);
#endif
  return true;
}
auto ReadEntityIdentity(const Json& item, SceneEntityDocument& entity, std::set<uint64_t>& entityIds,
                        const std::string& context, std::string& fieldError) -> bool {
  if (!ReadId(item, entity.id, fieldError) || !entityIds.insert(entity.id).second) {
    fieldError = context + ".id must be a unique positive integer";
  } else if (item.contains("name")) {
    if (!item.at("name").is_string()) {
      fieldError = context + ".name must be a string";
    } else {
      entity.name = item.at("name").get<std::string>();
    }
  }
  if (!fieldError.empty()) {
    return false;
  }
  if (!ReadBool(item, "sceneRoot", entity.sceneRoot, fieldError)) {
    fieldError = context + "." + fieldError;
    return false;
  }
  if (!ReadOptionalGuid(item, "modelGuid", entity.modelGuid, fieldError) ||
      !ReadOptionalGuid(item, "materialGuid", entity.materialGuid, fieldError)) {
    fieldError = context + "." + fieldError;
    return false;
  }
  if (item.contains("primitiveType") && !item.at("primitiveType").is_null()) {
    if (!item.at("primitiveType").is_string()) {
      fieldError = context + ".primitiveType must be a string or null";
      return false;
    }
    entity.primitiveType = item.at("primitiveType").get<std::string>();
    if (*entity.primitiveType != "cube" && *entity.primitiveType != "sphere" && *entity.primitiveType != "plane" &&
        *entity.primitiveType != "cone") {
      fieldError = context + ".primitiveType is unsupported";
      return false;
    }
  }
  if (entity.sceneRoot) {
    if (entity.modelGuid || entity.primitiveType) {
      fieldError = context + " scene root cannot specify a model or primitive";
      return false;
    }
  } else if (entity.modelGuid && entity.primitiveType) {
    fieldError = context + " cannot specify both modelGuid and primitiveType";
    return false;
  }
  return true;
}

auto ReadEntityMaterials(const Json& item, SceneEntityDocument& entity, SceneDocumentResult& result,
                         const std::string& context, std::string& fieldError) -> bool {
  const std::string materialContext = entity.name.empty() ? context : context + " ('" + entity.name + "')";
  if (item.contains("material") && !item.at("material").is_null()) {
    SceneMaterialDocument material;
    if (!ReadMaterial(item.at("material"), material, result.warnings, materialContext + ".material", fieldError)) {
      const std::string detail = std::move(fieldError);
      fieldError = context;
      fieldError += ".material: ";
      fieldError += detail;
      return false;
    }
    entity.material = std::move(material);
  }
  if (!item.contains("materialOverrides")) {
    return true;
  }
  const Json& overrides = item.at("materialOverrides");
  if (!overrides.is_array()) {
    fieldError = context + ".materialOverrides must be an array";
    return false;
  }
  for (size_t materialIndex = 0; materialIndex < overrides.size(); ++materialIndex) {
    if (overrides.at(materialIndex).is_null()) {
      entity.materialOverrides.emplace_back(std::nullopt);
      continue;
    }
    SceneMaterialDocument material;
    const std::string overrideContext = materialContext + ".materialOverrides[" + std::to_string(materialIndex) + "]";
    if (!ReadMaterial(overrides.at(materialIndex), material, result.warnings, overrideContext, fieldError)) {
      const std::string detail = std::move(fieldError);
      fieldError = overrideContext;
      fieldError += ": ";
      fieldError += detail;
      return false;
    }
    entity.materialOverrides.emplace_back(std::move(material));
  }
  return true;
}

auto ReadEntityTransform(const Json& item, SceneEntityDocument& entity, const std::string& context,
                         std::string& fieldError) -> bool {
  if ((item.contains("position") &&
       !ReadFloatArray(item.at("position"), entity.position, context + ".position", fieldError)) ||
      (item.contains("rotation") &&
       !ReadFloatArray(item.at("rotation"), entity.rotation, context + ".rotation", fieldError)) ||
      (item.contains("scale") && !ReadFloatArray(item.at("scale"), entity.scale, context + ".scale", fieldError)) ||
      !ReadBool(item, "flipHorizontal", entity.flipHorizontal, fieldError) ||
      !ReadBool(item, "flipVertical", entity.flipVertical, fieldError)) {
    return false;
  }
  const float rotationLengthSquared =
      (entity.rotation.at(0) * entity.rotation.at(0)) + (entity.rotation.at(1) * entity.rotation.at(1)) +
      (entity.rotation.at(2) * entity.rotation.at(2)) + (entity.rotation.at(3) * entity.rotation.at(3));
  if (!std::isfinite(rotationLengthSquared) || rotationLengthSquared <= 1.0e-12F) {
    fieldError = context + ".rotation must be a non-zero quaternion";
    return false;
  }
  return true;
}

auto ReadEntityRenderSettings(const Json& item, SceneEntityDocument& entity, SceneDocumentResult& result,
                              const std::string& context, std::string& fieldError) -> bool {
  if (!item.contains("render")) {
    return true;
  }
  const Json& render = item.at("render");
  if (!render.is_object()) {
    fieldError = context + ".render must be an object";
    return false;
  }
  KnownKeys(render, {"visible", "forceWireframe", "disableDepthTest", "castShadows", "receiveShadows"}, result.warnings,
            context + ".render");
  if (!ReadBool(render, "visible", entity.meshVisible, fieldError) ||
      !ReadBool(render, "forceWireframe", entity.forceWireframe, fieldError) ||
      !ReadBool(render, "disableDepthTest", entity.disableDepthTest, fieldError) ||
      !ReadBool(render, "castShadows", entity.castShadows, fieldError) ||
      !ReadBool(render, "receiveShadows", entity.receiveShadows, fieldError)) {
    fieldError = context + ".render." + fieldError;
    return false;
  }
  return true;
}

auto ReadEntity(const Json& item, SceneEntityDocument& entity, std::set<uint64_t>& entityIds,
                SceneDocumentResult& result, const std::string& context, std::string& fieldError) -> bool {
  KnownKeys(item,
            {
                "id",
                "name",
                "sceneRoot",
                "identityGuid",
                "parentGuid",
                "visible",
                "modelGuid",
                "materialGuid",
                "primitiveType",
                "hasMeshRenderer",
                "material",
                "materialOverrides",
                "position",
                "rotation",
                "scale",
                "flipHorizontal",
                "flipVertical",
                "render",
            },
            result.warnings, context);
  const bool identityLoaded = ReadEntityIdentity(item, entity, entityIds, context, fieldError) &&
                              ReadGuid(item, "identityGuid", entity.identityGuid, fieldError) &&
                              ReadGuid(item, "parentGuid", entity.parentGuid, fieldError);
  if (!identityLoaded) {
    return false;
  }
  if (!ReadBool(item, "hasMeshRenderer", entity.hasMeshRenderer, fieldError)) {
    fieldError = context + "." + fieldError;
    return false;
  }
  if (entity.sceneRoot) {
    const bool hasMaterial = (item.contains("material") && !item.at("material").is_null()) ||
                             (item.contains("materialOverrides") &&
                              (!item.at("materialOverrides").is_array() || !item.at("materialOverrides").empty()));
    if (entity.parentGuid || entity.materialGuid || hasMaterial) {
      fieldError = context + " scene root cannot have a parent or material";
      return false;
    }
    if (item.contains("visible") || item.contains("position") || item.contains("rotation") || item.contains("scale") ||
        item.contains("flipHorizontal") || item.contains("flipVertical") || item.contains("render")) {
      result.warnings.push_back(context + " legacy scene root transform/visibility fields ignored");
    }
    return true;
  }
  const bool loaded = ReadBool(item, "visible", entity.visible, fieldError) &&
                      ReadEntityMaterials(item, entity, result, context, fieldError) &&
                      ReadEntityTransform(item, entity, context, fieldError) &&
                      ReadEntityRenderSettings(item, entity, result, context, fieldError);
  if (!loaded) {
    return false;
  }
  entity.hasMeshRenderer = HasMeshRenderer(entity);
  if (entity.sceneRoot &&
      (entity.parentGuid || entity.materialGuid || entity.material || !entity.materialOverrides.empty())) {
    fieldError = context + " scene root cannot have a parent or material";
    return false;
  }
  return true;
}

auto LoadEntityArray(const Json& root, SceneDocumentResult& result, const std::filesystem::path& path,
                     std::string& fieldError) -> bool {
  if (!root.contains("entities")) {
    return true;
  }
  const Json& entities = root.at("entities");
  if (!entities.is_array()) {
    result.error = FileContext(path, "entities must be an array");
    return false;
  }
  std::set<uint64_t> entityIds;
  for (size_t index = 0; index < entities.size(); ++index) {
    const Json& item = entities.at(index);
    const std::string context = "entities[" + std::to_string(index) + "]";
    if (!item.is_object()) {
      result.error = FileContext(path, context + " must be an object");
      return false;
    }
    SceneEntityDocument entity;
    if (!ReadEntity(item, entity, entityIds, result, context, fieldError)) {
      result.error = FileContext(path, fieldError);
      return false;
    }
    result.document.entities.push_back(std::move(entity));
  }
  if (std::ranges::count(result.document.entities, true, &SceneEntityDocument::sceneRoot) > 1) {
    result.error = FileContext(path, "document contains multiple scene roots");
    return false;
  }
  return true;
}

auto LoadLightArray(const Json& root, SceneDocumentResult& result, const std::filesystem::path& path,
                    std::string& fieldError) -> bool {
  std::set<uint64_t> lightIds;
  if (root.contains("lights")) {
    if (!root.at("lights").is_array()) {
      result.error = FileContext(path, "lights must be an array");
      return false;
    }
    for (size_t index = 0; index < root.at("lights").size(); ++index) {
      const Json& item = root.at("lights").at(index);
      const std::string context = "lights[" + std::to_string(index) + "]";
      if (!item.is_object()) {
        result.error = FileContext(path, context + " must be an object");
        return false;
      }
      KnownKeys(item,
                {
                    "id",
                    "name",
                    "identityGuid",
                    "parentGuid",
                    "visible",
                    "direction",
                    "position",
                    "rotation",
                    "scale",
                    "flipHorizontal",
                    "flipVertical",
                    "color",
                    "intensity",
                    "range",
                    "innerAngleDegrees",
                    "outerAngleDegrees",
                    "shadowBias",
                    "shadowNormalBias",
                    "type",
                    "usePosition",
                    "castShadows",
                },
                result.warnings, context);
      SceneLightDocument light;
      if (item.contains("name")) {
        if (!item.at("name").is_string()) {
          result.error = FileContext(path, context + ".name must be a string");
          return false;
        }
        light.name = item.at("name").get<std::string>();
      }
      if (!ReadId(item, light.id, fieldError) || !lightIds.insert(light.id).second ||
          !ReadGuid(item, "identityGuid", light.identityGuid, fieldError) ||
          !ReadGuid(item, "parentGuid", light.parentGuid, fieldError) ||
          !ReadBool(item, "visible", light.visible, fieldError) ||
          !ReadArrayField(item, "direction", light.direction, fieldError) ||
          !ReadArrayField(item, "position", light.position, fieldError) ||
          !ReadArrayField(item, "rotation", light.rotation, fieldError) ||
          !ReadArrayField(item, "scale", light.scale, fieldError) ||
          !ReadBool(item, "flipHorizontal", light.flipHorizontal, fieldError) ||
          !ReadBool(item, "flipVertical", light.flipVertical, fieldError) ||
          !ReadArrayField(item, "color", light.color, fieldError) ||
          !ReadNumber(item, "intensity", light.intensity, fieldError) ||
          !ReadNumber(item, "range", light.range, fieldError) ||
          !ReadNumber(item, "innerAngleDegrees", light.innerAngleDegrees, fieldError) ||
          !ReadNumber(item, "outerAngleDegrees", light.outerAngleDegrees, fieldError) ||
          !ReadNumber(item, "shadowBias", light.shadowBias, fieldError) ||
          !ReadNumber(item, "shadowNormalBias", light.shadowNormalBias, fieldError) ||
          !ReadBool(item, "usePosition", light.usePosition, fieldError) ||
          !ReadBool(item, "castShadows", light.castShadows, fieldError)) {
        result.error = FileContext(path, context + ": " + (fieldError.empty() ? "duplicate light ID" : fieldError));
        return false;
      }
      if (item.contains("type")) {
        if (!item.at("type").is_string()) {
          result.error = FileContext(path, context + ".type must be a string");
          return false;
        }
        light.type = item.at("type").get<std::string>();
      } else {
        light.type = light.usePosition ? "spot" : "directional";
      }
      const float directionLengthSquared = (light.direction.at(0) * light.direction.at(0)) +
                                           (light.direction.at(1) * light.direction.at(1)) +
                                           (light.direction.at(2) * light.direction.at(2));
      const float rotationLengthSquared =
          (light.rotation.at(0) * light.rotation.at(0)) + (light.rotation.at(1) * light.rotation.at(1)) +
          (light.rotation.at(2) * light.rotation.at(2)) + (light.rotation.at(3) * light.rotation.at(3));
      const bool invalidDirection =
          !std::isfinite(directionLengthSquared) || (light.type != "point" && directionLengthSquared <= 1.0e-12F);
      if (invalidDirection || !std::isfinite(rotationLengthSquared) || rotationLengthSquared <= 1.0e-12F ||
          (light.type != "directional" && light.type != "point" && light.type != "spot") ||
          std::ranges::any_of(light.scale, [](float value) { return !std::isfinite(value) || value <= 0.0F; }) ||
          light.color.at(0) < 0.0F || light.color.at(1) < 0.0F || light.color.at(2) < 0.0F || light.intensity < 0.0F ||
          ((light.type == "point" || light.type == "spot") && light.range <= 0.0F) ||
          (light.type == "spot" &&
           (light.innerAngleDegrees < 0.0F || light.outerAngleDegrees <= light.innerAngleDegrees ||
            light.outerAngleDegrees > 89.5F)) ||
          light.shadowBias < 0.0F || light.shadowNormalBias < 0.0F) {
        result.error = FileContext(path, context + " contains out-of-range light settings");
        return false;
      }
      result.document.lights.push_back(light);
    }
  }
  return true;
}

auto LoadCameraArray(const Json& root, SceneDocumentResult& result, const std::filesystem::path& path,
                     std::string& fieldError) -> bool {
  std::set<uint64_t> cameraIds;
  if (root.contains("cameras")) {
    if (!root.at("cameras").is_array()) {
      result.error = FileContext(path, "cameras must be an array");
      return false;
    }
    for (size_t index = 0; index < root.at("cameras").size(); ++index) {
      const Json& item = root.at("cameras").at(index);
      const std::string context = "cameras[" + std::to_string(index) + "]";
      if (!item.is_object()) {
        result.error = FileContext(path, context + " must be an object");
        return false;
      }
      KnownKeys(item,
                {
                    "id",
                    "name",
                    "identityGuid",
                    "parentGuid",
                    "visible",
                    "position",
                    "rotation",
                    "scale",
                    "flipHorizontal",
                    "flipVertical",
                    "fovRadians",
                    "nearPlane",
                    "farPlane",
                    "outputWidth",
                    "outputHeight",
                    "priority",
                    "active",
                    "ambientLightOverride",
                    "postProcessing",
                    "ambientOcclusion",
                },
                result.warnings, context);
      SceneCameraDocument camera;
      if (item.contains("name")) {
        if (!item.at("name").is_string()) {
          result.error = FileContext(path, context + ".name must be a string");
          return false;
        }
        camera.name = item.at("name").get<std::string>();
      }
      if (!ReadId(item, camera.id, fieldError) || !cameraIds.insert(camera.id).second ||
          !ReadGuid(item, "identityGuid", camera.identityGuid, fieldError) ||
          !ReadGuid(item, "parentGuid", camera.parentGuid, fieldError) ||
          !ReadBool(item, "visible", camera.visible, fieldError) ||
          !ReadArrayField(item, "position", camera.position, fieldError) ||
          !ReadArrayField(item, "rotation", camera.rotation, fieldError) ||
          !ReadArrayField(item, "scale", camera.scale, fieldError) ||
          !ReadBool(item, "flipHorizontal", camera.flipHorizontal, fieldError) ||
          !ReadBool(item, "flipVertical", camera.flipVertical, fieldError) ||
          !ReadNumber(item, "fovRadians", camera.fovRadians, fieldError) ||
          !ReadNumber(item, "nearPlane", camera.nearPlane, fieldError) ||
          !ReadNumber(item, "farPlane", camera.farPlane, fieldError) ||
          !ReadPositiveDimension(item, "outputWidth", camera.outputWidth, fieldError) ||
          !ReadPositiveDimension(item, "outputHeight", camera.outputHeight, fieldError)) {
        result.error = FileContext(path, context + ": " + (fieldError.empty() ? "duplicate camera ID" : fieldError));
        return false;
      }
      if (item.contains("priority")) {
        const Json& priority = item.at("priority");
        const bool isOutOfRange =
            priority.is_number_unsigned()
                ? priority.get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())
                : !priority.is_number_integer() || priority.get<int64_t>() < std::numeric_limits<int32_t>::min() ||
                      priority.get<int64_t>() > std::numeric_limits<int32_t>::max();
        if (isOutOfRange) {
          result.error = FileContext(path, context + ".priority must be a 32-bit integer");
          return false;
        }
        camera.priority = item.at("priority").get<int32_t>();
      }
      if (!ReadBool(item, "active", camera.active, fieldError)) {
        std::string fieldContext = context;
        fieldContext.append(".").append(fieldError);
        result.error = FileContext(path, fieldContext);
        return false;
      }
      if (item.contains("ambientLightOverride") && !item.at("ambientLightOverride").is_null()) {
        AmbientLightSettings overrideSettings;
        if (!ReadAmbientLight(item.at("ambientLightOverride"), overrideSettings, fieldError)) {
          std::string fieldContext = context;
          fieldContext.append(".ambientLightOverride: ").append(fieldError);
          result.error = FileContext(path, fieldContext);
          return false;
        }
        camera.ambientLightOverride = overrideSettings;
      }
      if (item.contains("postProcessing") && !item.at("postProcessing").is_null()) {
        Ecs::CameraPostProcessing postProcessing;
        if (!ReadCameraPostProcessing(item.at("postProcessing"), postProcessing, fieldError)) {
          std::string fieldContext = context;
          fieldContext.append(".postProcessing: ").append(fieldError);
          result.error = FileContext(path, fieldContext);
          return false;
        }
        camera.postProcessing = std::move(postProcessing);
      }
      if (item.contains("ambientOcclusion") && !item.at("ambientOcclusion").is_null()) {
        if (!camera.postProcessing) {
          camera.postProcessing.emplace();
        }
        if (!ReadCameraAmbientOcclusion(item.at("ambientOcclusion"), *camera.postProcessing, fieldError)) {
          std::string fieldContext = context;
          fieldContext.append(".ambientOcclusion: ").append(fieldError);
          result.error = FileContext(path, fieldContext);
          return false;
        }
      }
      const float rotationLengthSquared =
          (camera.rotation.at(0) * camera.rotation.at(0)) + (camera.rotation.at(1) * camera.rotation.at(1)) +
          (camera.rotation.at(2) * camera.rotation.at(2)) + (camera.rotation.at(3) * camera.rotation.at(3));
      if (camera.fovRadians <= 0.0F || camera.fovRadians >= std::numbers::pi_v<float> || camera.nearPlane <= 0.0F ||
          camera.farPlane <= camera.nearPlane || camera.outputWidth == 0 || camera.outputHeight == 0 ||
          rotationLengthSquared <= 1.0e-12F ||
          std::ranges::any_of(camera.scale, [](float value) { return !std::isfinite(value) || value <= 0.0F; })) {
        result.error = FileContext(path, context + " contains invalid projection or rotation values");
        return false;
      }
      result.document.cameras.push_back(camera);
    }
  }
  return true;
}

} // namespace

auto SceneSerializer::Load(const std::filesystem::path& path) -> SceneDocumentResult {
  SceneDocumentResult result;
  Json root;
  try {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      result.error = FileContext(path, "could not open scene file");
      return result;
    }
    input >> root;
  } catch (const Json::exception& exception) {
    result.error = FileContext(path, std::string("invalid JSON: ") + exception.what());
    return result;
  } catch (const std::ios_base::failure& exception) {
    result.error = FileContext(path, std::string("could not read scene file: ") + exception.what());
    return result;
  }
  if (!root.is_object()) {
    result.error = FileContext(path, "scene root must be an object");
    return result;
  }
  KnownKeys(root,
            {"schemaVersion", "coordinateSystem", "unitsPerMeter", "ambientLight", "skybox", "entities", "lights",
             "cameras", "editor"},
            result.warnings, "scene");
  if (!root.contains("schemaVersion") || !root.at("schemaVersion").is_number_unsigned()) {
    result.error = FileContext(path, "schemaVersion must be an unsigned integer");
    return result;
  }
  result.document.schemaVersion = root.at("schemaVersion").get<uint32_t>();
  if (result.document.schemaVersion < 1 || result.document.schemaVersion > CurrentSceneSchemaVersion) {
    result.error =
        FileContext(path, "unsupported scene schema version " + std::to_string(result.document.schemaVersion));
    return result;
  }
  if (root.contains("coordinateSystem") &&
      (!root.at("coordinateSystem").is_string() || root.at("coordinateSystem") != "right-handed-y-up")) {
    result.error = FileContext(path, "coordinateSystem must be 'right-handed-y-up'");
    return result;
  }
  std::string fieldError;
  if (!ReadNumber(root, "unitsPerMeter", result.document.unitsPerMeter, fieldError) ||
      result.document.unitsPerMeter <= 0.0F) {
    result.error = FileContext(path, fieldError.empty() ? "unitsPerMeter must be positive" : fieldError);
    return result;
  }
  if (root.contains("ambientLight") &&
      !ReadAmbientLight(root.at("ambientLight"), result.document.ambientLight, fieldError)) {
    result.error = FileContext(path, "ambientLight: " + fieldError);
    return result;
  }
  if (root.contains("skybox") && !root.at("skybox").is_null()) {
    SceneSkyboxDocument skybox;
    if (!ReadSkybox(root.at("skybox"), skybox, result.warnings, fieldError)) {
      result.error = FileContext(path, "skybox: " + fieldError);
      return result;
    }
    result.document.skybox = std::move(skybox);
  }
  if (!LoadEntityArray(root, result, path, fieldError) || !LoadLightArray(root, result, path, fieldError) ||
      !LoadCameraArray(root, result, path, fieldError)) {
    return result;
  }
  return result;
}

auto SceneSerializer::SaveAtomic(const std::filesystem::path& path, const SceneDocument& document, std::string& error)
    -> bool {
  static std::atomic<uint64_t> temporaryFileSequence = 0;
  error.clear();
  if (!ValidateSceneHeader(path, document, error) || !ValidateEntities(path, document, error) ||
      !ValidateLights(path, document, error) || !ValidateCameras(path, document, error)) {
    return false;
  }
  std::filesystem::path temporaryPath = path;
  temporaryPath += ".tmp." + std::to_string(temporaryFileSequence.fetch_add(1, std::memory_order_relaxed));
  try {
    if (WriteSceneAtomically(path, temporaryPath, document, error)) {
      return true;
    }
    std::error_code ignored;
    std::filesystem::remove(temporaryPath, ignored);
    return false;
  } catch (const std::exception& exception) {
    std::error_code ignored;
    std::filesystem::remove(temporaryPath, ignored);
    error = FileContext(path, std::string("save failed: ") + exception.what());
    return false;
  }
}
} // namespace Engine
