#include "rendering/include/renderer/brdf/BrdfAnalysis.h"

#include "rendering/include/renderer/brdf/Brdf.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace Engine::Renderer::BrdfAnalysis {
namespace {

constexpr float RedLuminanceWeight = 0.2126F;
constexpr float GreenLuminanceWeight = 0.7152F;
constexpr float BlueLuminanceWeight = 0.0722F;
constexpr uint32_t MaximumSampleCount = 4096;

float Luminance(float red, float green, float blue) noexcept {
  return RedLuminanceWeight * red + GreenLuminanceWeight * green + BlueLuminanceWeight * blue;
}

float SafeChannel(float value) noexcept { return ClampFinite(value, 0.0F, 0.0F, 1.0F); }

DirectX::XMFLOAT3 EvaluateMicrofacetResponse(BrdfModel model, const DirectX::XMFLOAT3& view,
                                             const DirectX::XMFLOAT3& light, const DirectX::XMFLOAT3& diffuseColor,
                                             const DirectX::XMFLOAT3& f0, float alpha, float roughness) noexcept {
  constexpr DirectX::XMFLOAT3 normal{0.0F, 0.0F, 1.0F};
  switch (model) {
  case BrdfModel::GGX:
    return Brdf::EvaluateGgxBrdf(normal, view, light, diffuseColor, f0, alpha);
  case BrdfModel::Beckmann:
    return Brdf::EvaluateBeckmannBrdf(normal, view, light, diffuseColor, f0, alpha);
  case BrdfModel::Gauss:
    return Brdf::EvaluateGaussBrdf(normal, view, light, diffuseColor, f0, alpha);
  case BrdfModel::BlinnPhong:
    return Brdf::EvaluateBlinnPhongBrdf(normal, view, light, diffuseColor, f0, alpha);
  case BrdfModel::AshikhminShirley:
    return Brdf::EvaluateAshikhminShirleyBrdf(normal, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, view, light, diffuseColor,
                                              f0, roughness, roughness);
  case BrdfModel::Schlick:
    return Brdf::EvaluateSchlickBrdf(normal, view, light, diffuseColor, f0, alpha);
  case BrdfModel::Sawicki:
    return Brdf::EvaluateSawickiBrdf(normal, view, light, diffuseColor, f0, alpha);
  default:
    return {0.0F, 0.0F, 0.0F};
  }
}

} // namespace

Result<BrdfResponseCurve> AnalyzeBrdfResponse(const MaterialProperties& material, BrdfModel model,
                                              uint32_t sampleCount) {
  if (sampleCount < 2 || sampleCount > MaximumSampleCount) {
    return std::unexpected(MakeError("BRDF response sample count must be between 2 and 4096"));
  }
  if (static_cast<uint32_t>(model) > static_cast<uint32_t>(BrdfModel::Sawicki)) {
    return std::unexpected(MakeError("BRDF response model is unsupported"));
  }

  const float roughness = ClampFinite(material.Roughness, 0.5F, 0.0F, 1.0F);
  const float alpha = Brdf::AlphaFromPerceptualRoughness(roughness);
  const float metallic = ClampFinite(material.Metallic, 0.0F, 0.0F, 1.0F);
  const float gamma = ClampFinite(material.GtrGamma, 2.0F, Brdf::MinGamma, Brdf::MaxGamma);
  const float red = SafeChannel(material.BaseColor.x);
  const float green = SafeChannel(material.BaseColor.y);
  const float blue = SafeChannel(material.BaseColor.z);
  const bool specularGlossiness = material.Workflow == MaterialWorkflow::SpecularGlossiness;
  const float explicitF0Red = SafeChannel(material.SpecularF0.x);
  const float explicitF0Green = SafeChannel(material.SpecularF0.y);
  const float explicitF0Blue = SafeChannel(material.SpecularF0.z);
  float f0Red = explicitF0Red;
  float f0Green = explicitF0Green;
  float f0Blue = explicitF0Blue;
  if (material.SpecularMode != DielectricSpecularMode::ExplicitF0) {
    const float ior = ClampFinite(material.Ior, 1.5F, MaterialMinIor, MaterialMaxIor);
    const float iorRatio = (ior - 1.0F) / (ior + 1.0F);
    f0Red = f0Green = f0Blue = iorRatio * iorRatio;
  }

  const float diffuseRed = specularGlossiness
                               ? red * (1.0F - (std::max)({explicitF0Red, explicitF0Green, explicitF0Blue}))
                               : red * (1.0F - metallic);
  const float diffuseGreen = specularGlossiness
                                 ? green * (1.0F - (std::max)({explicitF0Red, explicitF0Green, explicitF0Blue}))
                                 : green * (1.0F - metallic);
  const float diffuseBlue = specularGlossiness
                                ? blue * (1.0F - (std::max)({explicitF0Red, explicitF0Green, explicitF0Blue}))
                                : blue * (1.0F - metallic);
  const float brdfMetallic = specularGlossiness ? 0.0F : metallic;
  if (brdfMetallic > 0.0F) {
    f0Red = f0Red * (1.0F - brdfMetallic) + red * brdfMetallic;
    f0Green = f0Green * (1.0F - brdfMetallic) + green * brdfMetallic;
    f0Blue = f0Blue * (1.0F - brdfMetallic) + blue * brdfMetallic;
  }

  const float legacySpecularRed = specularGlossiness ? explicitF0Red : 0.04F * (1.0F - metallic) + red * metallic;
  const float legacySpecularGreen = specularGlossiness ? explicitF0Green : 0.04F * (1.0F - metallic) + green * metallic;
  const float legacySpecularBlue = specularGlossiness ? explicitF0Blue : 0.04F * (1.0F - metallic) + blue * metallic;
  const float legacyPower = 8.0F + (128.0F - 8.0F) * (1.0F - roughness);
  const DirectX::XMFLOAT3 view{0.0F, 0.0F, 1.0F};
  const DirectX::XMFLOAT3 diffuseColor{diffuseRed, diffuseGreen, diffuseBlue};
  const DirectX::XMFLOAT3 f0{f0Red, f0Green, f0Blue};

  BrdfResponseCurve curve;
  curve.Model = model;
  curve.AnglesDegrees.reserve(sampleCount);
  curve.Response.reserve(sampleCount);
  const float angleStepRadians = (std::numbers::pi_v<float> * 0.5F) / static_cast<float>(sampleCount - 1);
  for (uint32_t index = 0; index < sampleCount; ++index) {
    const float angleRadians = angleStepRadians * static_cast<float>(index);
    const float angleDegrees = angleRadians * (180.0F / std::numbers::pi_v<float>);
    const float lightX = std::sin(angleRadians);
    const float lightZ = std::cos(angleRadians);
    const DirectX::XMFLOAT3 light{lightX, 0.0F, lightZ};
    const float responseCosine = (std::max)(lightZ, 0.0F);
    DirectX::XMFLOAT3 response{};

    if (model == BrdfModel::Legacy) {
      const float specular = std::pow(responseCosine, legacyPower - 1.0F);
      response = {diffuseRed + legacySpecularRed * specular, diffuseGreen + legacySpecularGreen * specular,
                  diffuseBlue + legacySpecularBlue * specular};
    } else if (model != BrdfModel::GTR) {
      response = EvaluateMicrofacetResponse(model, view, light, diffuseColor, f0, alpha, roughness);
    } else if (responseCosine > Brdf::Epsilon) {
      const float halfX = lightX;
      const float halfZ = 1.0F + lightZ;
      const float halfLength = std::sqrt(halfX * halfX + halfZ * halfZ);
      const float noH = (std::clamp)(halfZ / halfLength, 0.0F, 1.0F);
      const float voH = noH;
      const float distribution = Brdf::EvaluateGtrNdf(noH, alpha, gamma);
      const float fresnelWeight = std::pow(1.0F - voH, 5.0F);
      const float fresnelRed = f0Red + (1.0F - f0Red) * fresnelWeight;
      const float fresnelGreen = f0Green + (1.0F - f0Green) * fresnelWeight;
      const float fresnelBlue = f0Blue + (1.0F - f0Blue) * fresnelWeight;
      const float noV = 1.0F;
      const float noL = responseCosine;
      const float alphaSquared = alpha * alpha;
      const float smithV = noL * std::sqrt((std::max)(alphaSquared + (1.0F - alphaSquared) * noV * noV, 0.0F));
      const float smithL = noV * std::sqrt((std::max)(alphaSquared + (1.0F - alphaSquared) * noL * noL, 0.0F));
      const float smithDenominator = smithV + smithL;
      const float geometry = smithDenominator > Brdf::Epsilon ? (2.0F * noV * noL) / smithDenominator : 0.0F;
      const float specularScale = distribution * geometry / (4.0F * noV * noL);
      response = {diffuseRed * (1.0F - fresnelRed) / Brdf::Pi + fresnelRed * specularScale,
                  diffuseGreen * (1.0F - fresnelGreen) / Brdf::Pi + fresnelGreen * specularScale,
                  diffuseBlue * (1.0F - fresnelBlue) / Brdf::Pi + fresnelBlue * specularScale};
    }

    const float luminance = Luminance(response.x, response.y, response.z);
    if (!std::isfinite(luminance)) {
      return std::unexpected(MakeError("BRDF response produced a non-finite sample"));
    }
    curve.AnglesDegrees.push_back(angleDegrees);
    curve.Response.push_back(luminance);
    curve.Measurements.Peak = (std::max)(curve.Measurements.Peak, luminance);
    curve.Measurements.Mean += luminance / static_cast<float>(sampleCount);
    if (index > 0) {
      curve.Measurements.Integral += (curve.Response[index - 1] + luminance) * 0.5F * angleStepRadians;
    }
  }
  return curve;
}

Result<SceneLuminanceMeasurements> MeasureSceneLuminance(uint32_t width, uint32_t height, std::span<const float> rgba) {
  if (width == 0 || height == 0 || static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / height / 4U) {
    return std::unexpected(MakeError("scene luminance image dimensions are invalid"));
  }
  const size_t pixelCount = static_cast<size_t>(width) * height;
  if (rgba.size() != pixelCount * 4U) {
    return std::unexpected(MakeError("scene luminance RGBA data size does not match image dimensions"));
  }

  SceneLuminanceMeasurements measurements;
  double luminanceSum = 0.0;
  for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
    const size_t offset = pixelIndex * 4U;
    const float red = rgba[offset];
    const float green = rgba[offset + 1];
    const float blue = rgba[offset + 2];
    if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue) || !std::isfinite(rgba[offset + 3])) {
      return std::unexpected(MakeError("scene luminance image contains non-finite samples"));
    }
    const float luminance = Luminance((std::max)(red, 0.0F), (std::max)(green, 0.0F), (std::max)(blue, 0.0F));
    measurements.Peak = (std::max)(measurements.Peak, luminance);
    luminanceSum += luminance;
  }
  measurements.Mean = static_cast<float>(luminanceSum / static_cast<double>(pixelCount));
  if (!std::isfinite(measurements.Mean) || !std::isfinite(measurements.Peak)) {
    return std::unexpected(MakeError("scene luminance measurement is non-finite"));
  }
  return measurements;
}

Result<SceneLuminanceMeasurements> MeasureSceneLuminance(const Engine::RenderOutput& output) {
  const Engine::RenderOutputPlane* finalColor = nullptr;
  for (const Engine::RenderOutputPlane& plane : output.GetPlanes()) {
    if (plane.Aov == Engine::RenderAov::FinalColor) {
      finalColor = &plane;
      break;
    }
  }
  if (finalColor == nullptr || finalColor->Pixels.empty()) {
    return std::unexpected(MakeError("scene luminance final-color plane is unavailable"));
  }
  if (finalColor->ChannelCount != 4) {
    return std::unexpected(MakeError("scene luminance final-color plane is not RGBA"));
  }
  return MeasureSceneLuminance(output.Width, output.Height, finalColor->Pixels);
}

} // namespace Engine::Renderer::BrdfAnalysis
