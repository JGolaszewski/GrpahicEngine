#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>

namespace Engine::Renderer::Brdf {
inline constexpr float Pi = 3.14159265358979323846f;
inline constexpr float MinAlpha = 1.0e-3f;
inline constexpr float MinRoughness = 0.03162278f;
inline constexpr float MinGamma = 1.0f;
inline constexpr float MaxGamma = 8.0f;
inline constexpr float Epsilon = 1.0e-6f;

inline float ClampUnit(float value) noexcept { return (std::clamp)(value, 0.0f, 1.0f); }

inline float AlphaFromPerceptualRoughness(float roughness) noexcept {
  const float safeRoughness = (std::clamp)(roughness, MinRoughness, 1.0f);
  return (std::max)(safeRoughness * safeRoughness, MinAlpha);
}

inline float GtrNormalization(float alpha, float gamma) noexcept {
  const float safeAlpha = (std::max)(alpha, MinAlpha);
  const float safeGamma = (std::clamp)(gamma, MinGamma, MaxGamma);
  const float alphaSquared = safeAlpha * safeAlpha;
  if (std::abs(alphaSquared - 1.0f) <= Epsilon) {
    return 1.0f / Pi;
  }

  const float logAlphaSquared = std::log(alphaSquared);
  const float exponent = (1.0f - safeGamma) * logAlphaSquared;
  const float denominator = -std::expm1(exponent);
  if (std::abs(safeGamma - 1.0f) <= Epsilon) {
    return (alphaSquared - 1.0f) / (Pi * logAlphaSquared);
  }

  return ((safeGamma - 1.0f) * (alphaSquared - 1.0f)) / (Pi * denominator);
}

inline float EvaluateGtrNdf(float noH, float alpha, float gamma) noexcept {
  const float safeNoH = ClampUnit(noH);
  const float safeAlpha = (std::max)(alpha, MinAlpha);
  const float safeGamma = (std::clamp)(gamma, MinGamma, MaxGamma);
  const float alphaSquared = safeAlpha * safeAlpha;
  const float denominator = 1.0f + (alphaSquared - 1.0f) * safeNoH * safeNoH;
  return GtrNormalization(safeAlpha, safeGamma) / std::pow((std::max)(denominator, Epsilon), safeGamma);
}

inline float EvaluateGgxNdf(float noH, float alpha) noexcept { return EvaluateGtrNdf(noH, alpha, 2.0f); }

/// Evaluates the normalized isotropic Beckmann normal distribution using slope roughness alpha.
inline float EvaluateBeckmannNdf(float noH, float alpha) noexcept {
  const float safeNoH = ClampUnit(noH);
  const float safeAlpha = (std::max)(alpha, MinAlpha);
  const float noHSquared = safeNoH * safeNoH;
  if (noHSquared <= Epsilon) {
    return 0.0f;
  }
  const float alphaSquared = safeAlpha * safeAlpha;
  const float denominator = Pi * alphaSquared * noHSquared * noHSquared;
  const float exponent = (noHSquared - 1.0f) / (alphaSquared * noHSquared);
  return std::exp(exponent) / (std::max)(denominator, Epsilon);
}

/// Evaluates the normalized isotropic Gaussian-slope normal distribution using slope width alpha.
inline float EvaluateGaussNdf(float noH, float alpha) noexcept {
  const float safeNoH = ClampUnit(noH);
  const float noHSquared = safeNoH * safeNoH;
  if (safeNoH <= 0.0f) {
    return 0.0f;
  }
  const float safeAlpha = (std::max)(alpha, MinAlpha);
  const float alphaSquared = safeAlpha * safeAlpha;
  const float noHSquaredSafe = (std::max)(noHSquared, Epsilon);
  const float tanSquared = (1.0f - noHSquared) / noHSquaredSafe;
  const float exponent = -tanSquared / (2.0f * alphaSquared);
  const float cosFourth = noHSquared * noHSquared;
  const float denominator = (std::max)(2.0f * Pi * alphaSquared * cosFourth, Epsilon);
  return std::exp(exponent) / denominator;
}

/// Evaluates the normalized isotropic Blinn-Phong normal distribution from slope roughness alpha.
inline float EvaluateBlinnPhongNdf(float noH, float alpha) noexcept {
  const float safeNoH = ClampUnit(noH);
  const float safeAlpha = (std::max)(alpha, MinAlpha);
  const float exponent = (std::max)(2.0f / (safeAlpha * safeAlpha) - 2.0f, 0.0f);
  const float cosinePower = safeNoH == 0.0f && exponent == 0.0f ? 1.0f : std::pow(safeNoH, exponent);
  return ((exponent + 2.0f) / (2.0f * Pi)) * cosinePower;
}

/// Evaluates the normalized Ashikhmin-Shirley anisotropic NDF in a tangent frame.
/// @param halfTangent Half-vector projected on tangent, bitangent, and normal axes.
/// @param roughnessU Perceptual roughness along the tangent axis.
/// @param roughnessV Perceptual roughness along the bitangent axis.
inline float EvaluateAshikhminShirleyNdf(const DirectX::XMFLOAT3& halfTangent, float roughnessU,
                                         float roughnessV) noexcept {
  const float noH = ClampUnit(halfTangent.z);
  const float alphaU = AlphaFromPerceptualRoughness(std::isfinite(roughnessU) ? roughnessU : 0.5f);
  const float alphaV = AlphaFromPerceptualRoughness(std::isfinite(roughnessV) ? roughnessV : 0.5f);
  const float nu = (std::max)(2.0f / (alphaU * alphaU) - 2.0f, 0.0f);
  const float nv = (std::max)(2.0f / (alphaV * alphaV) - 2.0f, 0.0f);
  const float tangentSquared = halfTangent.x * halfTangent.x + halfTangent.y * halfTangent.y;
  const float exponent =
      tangentSquared > Epsilon
          ? (nu * halfTangent.x * halfTangent.x + nv * halfTangent.y * halfTangent.y) / tangentSquared
          : 0.5f * (nu + nv);
  const float cosPower = noH == 0.0f && exponent == 0.0f ? 1.0f : std::pow(noH, exponent);
  const float normalization = std::sqrt((nu + 2.0f) * (nv + 2.0f)) / (2.0f * Pi);
  return normalization * cosPower;
}

inline DirectX::XMFLOAT3 EvaluateGgxBrdf(const DirectX::XMFLOAT3& normal, const DirectX::XMFLOAT3& view,
                                         const DirectX::XMFLOAT3& light, const DirectX::XMFLOAT3& diffuseColor,
                                         const DirectX::XMFLOAT3& f0, float alpha) noexcept {
  using namespace DirectX;
  const XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&normal));
  const XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&view));
  const XMVECTOR l = XMVector3Normalize(XMLoadFloat3(&light));
  const float noV = (std::max)(XMVectorGetX(XMVector3Dot(n, v)), 0.0f);
  const float noL = (std::max)(XMVectorGetX(XMVector3Dot(n, l)), 0.0f);
  if (noV <= Epsilon || noL <= Epsilon) {
    return {0.0f, 0.0f, 0.0f};
  }

  const XMVECTOR h = XMVector3Normalize(XMVectorAdd(v, l));
  const float noH = ClampUnit(XMVectorGetX(XMVector3Dot(n, h)));
  const float voH = ClampUnit(XMVectorGetX(XMVector3Dot(v, h)));
  const float safeAlpha = (std::max)(alpha, MinAlpha);
  const float alphaSquared = safeAlpha * safeAlpha;
  const float smithV = noL * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noV * noV, 0.0f));
  const float smithL = noV * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noL * noL, 0.0f));
  const float geometry = (smithV + smithL) > Epsilon ? (2.0f * noV * noL) / (smithV + smithL) : 0.0f;
  const float fresnelWeight = std::pow(1.0f - voH, 5.0f);
  const float ndf = EvaluateGgxNdf(noH, safeAlpha);
  const float specularScale = ndf * geometry / (4.0f * noV * noL);

  XMFLOAT3 result;
  XMStoreFloat3(
      &result,
      XMVectorSet(diffuseColor.x * (1.0f - f0.x) / Pi + (f0.x + (1.0f - f0.x) * fresnelWeight) * specularScale,
                  diffuseColor.y * (1.0f - f0.y) / Pi + (f0.y + (1.0f - f0.y) * fresnelWeight) * specularScale,
                  diffuseColor.z * (1.0f - f0.z) / Pi + (f0.z + (1.0f - f0.z) * fresnelWeight) * specularScale, 0.0f));
  return result;
}

/// Evaluates the single-scatter isotropic Beckmann microfacet BRDF.
inline DirectX::XMFLOAT3 EvaluateBeckmannBrdf(const DirectX::XMFLOAT3& normal, const DirectX::XMFLOAT3& view,
                                              const DirectX::XMFLOAT3& light, const DirectX::XMFLOAT3& diffuseColor,
                                              const DirectX::XMFLOAT3& f0, float alpha) noexcept {
  using namespace DirectX;
  const XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&normal));
  const XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&view));
  const XMVECTOR l = XMVector3Normalize(XMLoadFloat3(&light));
  const float noV = (std::max)(XMVectorGetX(XMVector3Dot(n, v)), 0.0f);
  const float noL = (std::max)(XMVectorGetX(XMVector3Dot(n, l)), 0.0f);
  if (noV <= Epsilon || noL <= Epsilon) {
    return {0.0f, 0.0f, 0.0f};
  }

  const XMVECTOR h = XMVector3Normalize(XMVectorAdd(v, l));
  const float noH = ClampUnit(XMVectorGetX(XMVector3Dot(n, h)));
  const float voH = ClampUnit(XMVectorGetX(XMVector3Dot(v, h)));
  const float safeAlpha = (std::max)(alpha, MinAlpha);
  const float alphaSquared = safeAlpha * safeAlpha;
  const float smithV = noL * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noV * noV, 0.0f));
  const float smithL = noV * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noL * noL, 0.0f));
  const float geometry = (smithV + smithL) > Epsilon ? (2.0f * noV * noL) / (smithV + smithL) : 0.0f;
  const float fresnelWeight = std::pow(1.0f - voH, 5.0f);
  const float specularScale = EvaluateBeckmannNdf(noH, safeAlpha) * geometry / (4.0f * noV * noL);

  XMFLOAT3 result;
  XMStoreFloat3(
      &result,
      XMVectorSet(diffuseColor.x * (1.0f - f0.x) / Pi + (f0.x + (1.0f - f0.x) * fresnelWeight) * specularScale,
                  diffuseColor.y * (1.0f - f0.y) / Pi + (f0.y + (1.0f - f0.y) * fresnelWeight) * specularScale,
                  diffuseColor.z * (1.0f - f0.z) / Pi + (f0.z + (1.0f - f0.z) * fresnelWeight) * specularScale, 0.0f));
  return result;
}

/// Evaluates the single-scatter isotropic Gaussian microfacet BRDF.
inline DirectX::XMFLOAT3 EvaluateGaussBrdf(const DirectX::XMFLOAT3& normal, const DirectX::XMFLOAT3& view,
                                           const DirectX::XMFLOAT3& light, const DirectX::XMFLOAT3& diffuseColor,
                                           const DirectX::XMFLOAT3& f0, float alpha) noexcept {
  using namespace DirectX;
  const XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&normal));
  const XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&view));
  const XMVECTOR l = XMVector3Normalize(XMLoadFloat3(&light));
  const float noV = (std::max)(XMVectorGetX(XMVector3Dot(n, v)), 0.0f);
  const float noL = (std::max)(XMVectorGetX(XMVector3Dot(n, l)), 0.0f);
  if (noV <= Epsilon || noL <= Epsilon) {
    return {0.0f, 0.0f, 0.0f};
  }
  const XMVECTOR h = XMVector3Normalize(XMVectorAdd(v, l));
  const float noH = ClampUnit(XMVectorGetX(XMVector3Dot(n, h)));
  const float voH = ClampUnit(XMVectorGetX(XMVector3Dot(v, h)));
  const float safeAlpha = (std::max)(alpha, MinAlpha);
  const float alphaSquared = safeAlpha * safeAlpha;
  const float smithV = noL * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noV * noV, 0.0f));
  const float smithL = noV * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noL * noL, 0.0f));
  const float geometryDenominator = smithV + smithL;
  const float geometry = geometryDenominator > Epsilon ? (2.0f * noV * noL) / geometryDenominator : 0.0f;
  const float fresnelWeight = std::pow(1.0f - voH, 5.0f);
  const float specularScale = EvaluateGaussNdf(noH, safeAlpha) * geometry / (4.0f * noV * noL);
  XMFLOAT3 result;
  XMStoreFloat3(
      &result,
      XMVectorSet(diffuseColor.x * (1.0f - f0.x) / Pi + (f0.x + (1.0f - f0.x) * fresnelWeight) * specularScale,
                  diffuseColor.y * (1.0f - f0.y) / Pi + (f0.y + (1.0f - f0.y) * fresnelWeight) * specularScale,
                  diffuseColor.z * (1.0f - f0.z) / Pi + (f0.z + (1.0f - f0.z) * fresnelWeight) * specularScale, 0.0f));
  return result;
}

/// Evaluates the single-scatter isotropic Blinn-Phong microfacet BRDF.
inline DirectX::XMFLOAT3 EvaluateBlinnPhongBrdf(const DirectX::XMFLOAT3& normal, const DirectX::XMFLOAT3& view,
                                                const DirectX::XMFLOAT3& light, const DirectX::XMFLOAT3& diffuseColor,
                                                const DirectX::XMFLOAT3& f0, float alpha) noexcept {
  using namespace DirectX;
  const XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&normal));
  const XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&view));
  const XMVECTOR l = XMVector3Normalize(XMLoadFloat3(&light));
  const float noV = (std::max)(XMVectorGetX(XMVector3Dot(n, v)), 0.0f);
  const float noL = (std::max)(XMVectorGetX(XMVector3Dot(n, l)), 0.0f);
  if (noV <= Epsilon || noL <= Epsilon) {
    return {0.0f, 0.0f, 0.0f};
  }

  const XMVECTOR h = XMVector3Normalize(XMVectorAdd(v, l));
  const float noH = ClampUnit(XMVectorGetX(XMVector3Dot(n, h)));
  const float voH = ClampUnit(XMVectorGetX(XMVector3Dot(v, h)));
  const float safeAlpha = (std::max)(alpha, MinAlpha);
  const float alphaSquared = safeAlpha * safeAlpha;
  const float smithV = noL * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noV * noV, 0.0f));
  const float smithL = noV * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noL * noL, 0.0f));
  const float geometryDenominator = smithV + smithL;
  const float geometry = geometryDenominator > Epsilon ? (2.0f * noV * noL) / geometryDenominator : 0.0f;
  const float fresnelWeight = std::pow(1.0f - voH, 5.0f);
  const float specularScale = EvaluateBlinnPhongNdf(noH, safeAlpha) * geometry / (4.0f * noV * noL);
  XMFLOAT3 result;
  XMStoreFloat3(
      &result,
      XMVectorSet(diffuseColor.x * (1.0f - f0.x) / Pi + (f0.x + (1.0f - f0.x) * fresnelWeight) * specularScale,
                  diffuseColor.y * (1.0f - f0.y) / Pi + (f0.y + (1.0f - f0.y) * fresnelWeight) * specularScale,
                  diffuseColor.z * (1.0f - f0.z) / Pi + (f0.z + (1.0f - f0.z) * fresnelWeight) * specularScale, 0.0f));
  return result;
}

/// Evaluates Ashikhmin-Shirley using the mesh tangent frame and independent perceptual roughness axes.
inline DirectX::XMFLOAT3 EvaluateAshikhminShirleyBrdf(const DirectX::XMFLOAT3& normal, const DirectX::XMFLOAT3& tangent,
                                                      const DirectX::XMFLOAT3& bitangent, const DirectX::XMFLOAT3& view,
                                                      const DirectX::XMFLOAT3& light,
                                                      const DirectX::XMFLOAT3& diffuseColor,
                                                      const DirectX::XMFLOAT3& f0, float roughnessU,
                                                      float roughnessV) noexcept {
  using namespace DirectX;
  const XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&normal));
  const XMVECTOR t = XMVector3Normalize(XMLoadFloat3(&tangent));
  const XMVECTOR b = XMVector3Normalize(XMLoadFloat3(&bitangent));
  const XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&view));
  const XMVECTOR l = XMVector3Normalize(XMLoadFloat3(&light));
  const float noV = (std::max)(XMVectorGetX(XMVector3Dot(n, v)), 0.0f);
  const float noL = (std::max)(XMVectorGetX(XMVector3Dot(n, l)), 0.0f);
  if (noV <= Epsilon || noL <= Epsilon)
    return {0.0f, 0.0f, 0.0f};
  const XMVECTOR h = XMVector3Normalize(XMVectorAdd(v, l));
  const XMFLOAT3 halfTangent{XMVectorGetX(XMVector3Dot(h, t)), XMVectorGetX(XMVector3Dot(h, b)),
                             XMVectorGetX(XMVector3Dot(h, n))};
  const float voH = ClampUnit(XMVectorGetX(XMVector3Dot(v, h)));
  const float safeRoughnessU = std::isfinite(roughnessU) ? roughnessU : 0.5f;
  const float safeRoughnessV = std::isfinite(roughnessV) ? roughnessV : 0.5f;
  const float alpha =
      (std::max)(AlphaFromPerceptualRoughness(safeRoughnessU), AlphaFromPerceptualRoughness(safeRoughnessV));
  const float alphaSquared = alpha * alpha;
  const float smithV = noL * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noV * noV, 0.0f));
  const float smithL = noV * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noL * noL, 0.0f));
  const float geometry = smithV + smithL > Epsilon ? 2.0f * noV * noL / (smithV + smithL) : 0.0f;
  const float fresnelWeight = std::pow(1.0f - voH, 5.0f);
  const float specularScale =
      EvaluateAshikhminShirleyNdf(halfTangent, safeRoughnessU, safeRoughnessV) * geometry / (4.0f * noV * noL);
  return {diffuseColor.x * (1.0f - f0.x) / Pi + (f0.x + (1.0f - f0.x) * fresnelWeight) * specularScale,
          diffuseColor.y * (1.0f - f0.y) / Pi + (f0.y + (1.0f - f0.y) * fresnelWeight) * specularScale,
          diffuseColor.z * (1.0f - f0.z) / Pi + (f0.z + (1.0f - f0.z) * fresnelWeight) * specularScale};
}
} // namespace Engine::Renderer::Brdf
