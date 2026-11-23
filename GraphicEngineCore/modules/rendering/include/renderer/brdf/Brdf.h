#pragma once

#include "renderer/brdf/Sawicki.h"
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

/// Evaluates the normalized isotropic Schlick rational normal distribution using alpha roughness.
inline float EvaluateSchlickNdf(float noH, float alpha) noexcept {
  const float safeNoH = ClampUnit(noH);
  const float safeAlpha = std::isfinite(alpha) ? (std::max)(alpha, MinAlpha) : MinAlpha;
  const float m = (std::clamp)(safeAlpha, MinAlpha, 1.0f);
  const float x = safeNoH + m - 1.0f;
  if (safeNoH <= 0.0f || x <= 0.0f) {
    return 0.0f;
  }
  const float inner = (m - 1.0f) * x * x + m * m;
  const float denominator = Pi * safeNoH * inner * inner;
  return (m * m * m * x) / (std::max)(denominator, 1.0e-30f);
}

/// Evaluates the normalized isotropic Sawicki normal distribution using Sawicki's N_DS coefficient.
inline float EvaluateSawickiNdf(float noH, float nds) noexcept {
  const float safeNoH = ClampUnit(noH);
  const float safeNds = std::isfinite(nds) ? (std::clamp)(nds, -2.999f, 3.0e6f) : 0.0f;
  const float shifted = (safeNds + 3.0f) * (1.0f - safeNoH) + 4.0f * safeNoH;
  const float denominator = Pi * shifted * shifted * shifted * shifted;
  return 96.0f * (3.0f + safeNds) * safeNoH / (std::max)(denominator, 1.0e-30f);
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

/// Evaluates GGX with the same angular Schlick Fresnel diffuse partition as the D3D11 BRDF shader.
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
  XMStoreFloat3(&result, XMVectorSet(diffuseColor.x * (1.0f - f0.x) * (1.0f - fresnelWeight) / Pi +
                                         (f0.x + (1.0f - f0.x) * fresnelWeight) * specularScale,
                                     diffuseColor.y * (1.0f - f0.y) * (1.0f - fresnelWeight) / Pi +
                                         (f0.y + (1.0f - f0.y) * fresnelWeight) * specularScale,
                                     diffuseColor.z * (1.0f - f0.z) * (1.0f - fresnelWeight) / Pi +
                                         (f0.z + (1.0f - f0.z) * fresnelWeight) * specularScale,
                                     0.0f));
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
  XMStoreFloat3(&result, XMVectorSet(diffuseColor.x * (1.0f - f0.x) * (1.0f - fresnelWeight) / Pi +
                                         (f0.x + (1.0f - f0.x) * fresnelWeight) * specularScale,
                                     diffuseColor.y * (1.0f - f0.y) * (1.0f - fresnelWeight) / Pi +
                                         (f0.y + (1.0f - f0.y) * fresnelWeight) * specularScale,
                                     diffuseColor.z * (1.0f - f0.z) * (1.0f - fresnelWeight) / Pi +
                                         (f0.z + (1.0f - f0.z) * fresnelWeight) * specularScale,
                                     0.0f));
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
  XMStoreFloat3(&result, XMVectorSet(diffuseColor.x * (1.0f - f0.x) * (1.0f - fresnelWeight) / Pi +
                                         (f0.x + (1.0f - f0.x) * fresnelWeight) * specularScale,
                                     diffuseColor.y * (1.0f - f0.y) * (1.0f - fresnelWeight) / Pi +
                                         (f0.y + (1.0f - f0.y) * fresnelWeight) * specularScale,
                                     diffuseColor.z * (1.0f - f0.z) * (1.0f - fresnelWeight) / Pi +
                                         (f0.z + (1.0f - f0.z) * fresnelWeight) * specularScale,
                                     0.0f));
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
  XMStoreFloat3(&result, XMVectorSet(diffuseColor.x * (1.0f - f0.x) * (1.0f - fresnelWeight) / Pi +
                                         (f0.x + (1.0f - f0.x) * fresnelWeight) * specularScale,
                                     diffuseColor.y * (1.0f - f0.y) * (1.0f - fresnelWeight) / Pi +
                                         (f0.y + (1.0f - f0.y) * fresnelWeight) * specularScale,
                                     diffuseColor.z * (1.0f - f0.z) * (1.0f - fresnelWeight) / Pi +
                                         (f0.z + (1.0f - f0.z) * fresnelWeight) * specularScale,
                                     0.0f));
  return result;
}

/// Evaluates Ashikhmin-Shirley using an orthonormal frame around the shading normal and independent roughness axes.
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
  if (noV <= Epsilon || noL <= Epsilon) {
    return {0.0f, 0.0f, 0.0f};
  }
  const XMVECTOR h = XMVector3Normalize(XMVectorAdd(v, l));
  const XMVECTOR projectedTangent = XMVectorSubtract(t, XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, t))));
  const float projectedTangentLengthSquared = XMVectorGetX(XMVector3LengthSq(projectedTangent));
  XMVECTOR orthogonalTangent = projectedTangent;
  if (projectedTangentLengthSquared <= Epsilon) {
    const XMVECTOR fallbackAxis =
        std::abs(XMVectorGetY(n)) < 0.9F ? XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F) : XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F);
    orthogonalTangent = XMVector3Cross(fallbackAxis, n);
  }
  orthogonalTangent = XMVector3Normalize(orthogonalTangent);
  const XMVECTOR frameBitangent = XMVector3Cross(n, orthogonalTangent);
  const float handedness = XMVectorGetX(XMVector3Dot(frameBitangent, b)) < 0.0F ? -1.0F : 1.0F;
  const XMVECTOR orthogonalBitangent = XMVectorScale(frameBitangent, handedness);
  const XMFLOAT3 halfTangent{XMVectorGetX(XMVector3Dot(h, orthogonalTangent)),
                             XMVectorGetX(XMVector3Dot(h, orthogonalBitangent)), XMVectorGetX(XMVector3Dot(h, n))};
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
  return {diffuseColor.x * (1.0f - f0.x) * (1.0f - fresnelWeight) / Pi +
              (f0.x + (1.0f - f0.x) * fresnelWeight) * specularScale,
          diffuseColor.y * (1.0f - f0.y) * (1.0f - fresnelWeight) / Pi +
              (f0.y + (1.0f - f0.y) * fresnelWeight) * specularScale,
          diffuseColor.z * (1.0f - f0.z) * (1.0f - fresnelWeight) / Pi +
              (f0.z + (1.0f - f0.z) * fresnelWeight) * specularScale};
}

/// Evaluates the single-scatter isotropic Schlick microfacet BRDF.
inline DirectX::XMFLOAT3 EvaluateSchlickBrdf(const DirectX::XMFLOAT3& normal, const DirectX::XMFLOAT3& view,
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
  const float specularScale = EvaluateSchlickNdf(noH, safeAlpha) * geometry / (4.0f * noV * noL);
  return {diffuseColor.x * (1.0f - f0.x) * (1.0f - fresnelWeight) / Pi +
              (f0.x + (1.0f - f0.x) * fresnelWeight) * specularScale,
          diffuseColor.y * (1.0f - f0.y) * (1.0f - fresnelWeight) / Pi +
              (f0.y + (1.0f - f0.y) * fresnelWeight) * specularScale,
          diffuseColor.z * (1.0f - f0.z) * (1.0f - fresnelWeight) / Pi +
              (f0.z + (1.0f - f0.z) * fresnelWeight) * specularScale};
}

/// Evaluates the single-scatter isotropic Sawicki microfacet BRDF.
inline DirectX::XMFLOAT3 EvaluateSawickiBrdf(const DirectX::XMFLOAT3& normal, const DirectX::XMFLOAT3& view,
                                             const DirectX::XMFLOAT3& light, const DirectX::XMFLOAT3& diffuseColor,
                                             const DirectX::XMFLOAT3& f0, float alpha) noexcept {
  using namespace DirectX;
  const XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&normal));
  const XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&view));
  const XMVECTOR l = XMVector3Normalize(XMLoadFloat3(&light));
  const float noV = (std::max)(XMVectorGetX(XMVector3Dot(n, v)), 0.0f);
  const float noL = (std::max)(XMVectorGetX(XMVector3Dot(n, l)), 0.0f);
  if (noV <= Epsilon || noL <= Epsilon)
    return {0.0f, 0.0f, 0.0f};
  const XMVECTOR h = XMVector3Normalize(XMVectorAdd(v, l));
  const float noH = ClampUnit(XMVectorGetX(XMVector3Dot(n, h)));
  const float voH = ClampUnit(XMVectorGetX(XMVector3Dot(v, h)));
  const float safeAlpha = std::isfinite(alpha) ? (std::clamp)(alpha, MinAlpha, 1.0f) : MinAlpha;
  const float alphaSquared = safeAlpha * safeAlpha;
  const float smithV = noL * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noV * noV, 0.0f));
  const float smithL = noV * std::sqrt((std::max)(alphaSquared + (1.0f - alphaSquared) * noL * noL, 0.0f));
  const float geometryDenominator = smithV + smithL;
  const float geometry = geometryDenominator > Epsilon ? (2.0f * noV * noL) / geometryDenominator : 0.0f;
  const float fresnelWeight = std::pow(1.0f - voH, 5.0f);
  const XMFLOAT3 fresnel = {f0.x + (1.0f - f0.x) * fresnelWeight, f0.y + (1.0f - f0.y) * fresnelWeight,
                            f0.z + (1.0f - f0.z) * fresnelWeight};
  const float specularScale =
      EvaluateSawickiNdf(noH, SawickiNdsFromBeckmannAlpha(safeAlpha)) * geometry / (4.0f * noV * noL);
  return {diffuseColor.x * (1.0f - fresnel.x) / Pi + fresnel.x * specularScale,
          diffuseColor.y * (1.0f - fresnel.y) / Pi + fresnel.y * specularScale,
          diffuseColor.z * (1.0f - fresnel.z) / Pi + fresnel.z * specularScale};
}

/// Evaluates the Sawicki BRDF using metallic-roughness factors, matching the D3D11 material workflow.
inline DirectX::XMFLOAT3
EvaluateSawickiMetallicRoughnessBrdf(const DirectX::XMFLOAT3& normal, const DirectX::XMFLOAT3& view,
                                     const DirectX::XMFLOAT3& light, const DirectX::XMFLOAT3& baseColor, float metallic,
                                     const DirectX::XMFLOAT3& dielectricF0, float alpha) noexcept {
  const float safeMetallic = std::isfinite(metallic) ? ClampUnit(metallic) : 0.0f;
  const DirectX::XMFLOAT3 diffuseColor = {baseColor.x * (1.0f - safeMetallic), baseColor.y * (1.0f - safeMetallic),
                                          baseColor.z * (1.0f - safeMetallic)};
  const DirectX::XMFLOAT3 f0 = {dielectricF0.x + (baseColor.x - dielectricF0.x) * safeMetallic,
                                dielectricF0.y + (baseColor.y - dielectricF0.y) * safeMetallic,
                                dielectricF0.z + (baseColor.z - dielectricF0.z) * safeMetallic};
  return EvaluateSawickiBrdf(normal, view, light, diffuseColor, f0, alpha);
} // namespace Engine::Renderer::Brdf
