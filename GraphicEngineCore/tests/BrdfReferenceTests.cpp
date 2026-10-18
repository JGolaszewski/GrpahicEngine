#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/include/renderer/brdf/Brdf.h"
#include "rendering/src/render_graph/RenderView.h"
#include "scene/include/Scene.h"
#include "scene/include/components/EcsComponents.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <numbers>
#include <utility>

namespace {
void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

auto IntegrateNdf(float alpha, float gamma) -> float {
  constexpr uint32_t sampleCount = 20000;
  const float step = DirectX::XM_PIDIV2 / static_cast<float>(sampleCount);
  float integral = 0.0F;
  for (uint32_t i = 0; i < sampleCount; ++i) {
    const float theta = (static_cast<float>(i) + 0.5F) * step;
    const float cosine = std::cos(theta);
    const float sine = std::sin(theta);
    integral += ::Engine::Renderer::Brdf::EvaluateGtrNdf(cosine, alpha, gamma) * cosine * sine * step;
  }
  return 2.0F * ::Engine::Renderer::Brdf::Pi * integral;
}

auto IntegrateBeckmannNdf(float alpha) -> float {
  constexpr uint32_t sampleCount = 20000;
  const float step = DirectX::XM_PIDIV2 / static_cast<float>(sampleCount);
  float integral = 0.0F;
  for (uint32_t i = 0; i < sampleCount; ++i) {
    const float theta = (static_cast<float>(i) + 0.5F) * step;
    const float cosine = std::cos(theta);
    const float sine = std::sin(theta);
    integral += ::Engine::Renderer::Brdf::EvaluateBeckmannNdf(cosine, alpha) * cosine * sine * step;
  }
  return 2.0F * ::Engine::Renderer::Brdf::Pi * integral;
}

auto IntegrateGaussNdf(float alpha) -> float {
  constexpr uint32_t sampleCount = 20000;
  const float step = DirectX::XM_PIDIV2 / static_cast<float>(sampleCount);
  float integral = 0.0F;
  for (uint32_t i = 0; i < sampleCount; ++i) {
    const float theta = (static_cast<float>(i) + 0.5F) * step;
    const float cosine = std::cos(theta);
    const float sine = std::sin(theta);
    integral += ::Engine::Renderer::Brdf::EvaluateGaussNdf(cosine, alpha) * cosine * sine * step;
  }
  return 2.0F * ::Engine::Renderer::Brdf::Pi * integral;
}

auto IntegrateBlinnPhongNdf(float alpha) -> float {
  constexpr uint32_t sampleCount = 20000;
  const float step = DirectX::XM_PIDIV2 / static_cast<float>(sampleCount);
  float integral = 0.0F;
  for (uint32_t i = 0; i < sampleCount; ++i) {
    const float theta = (static_cast<float>(i) + 0.5F) * step;
    const float cosine = std::cos(theta);
    const float sine = std::sin(theta);
    integral += ::Engine::Renderer::Brdf::EvaluateBlinnPhongNdf(cosine, alpha) * cosine * sine * step;
  }
  return 2.0F * ::Engine::Renderer::Brdf::Pi * integral;
}

auto IntegrateSchlickNdf(float alpha) -> float {
  constexpr uint32_t sampleCount = 20000;
  const float step = DirectX::XM_PIDIV2 / static_cast<float>(sampleCount);
  float integral = 0.0F;
  for (uint32_t i = 0; i < sampleCount; ++i) {
    const float theta = (static_cast<float>(i) + 0.5F) * step;
    const float cosine = std::cos(theta);
    const float sine = std::sin(theta);
    integral += ::Engine::Renderer::Brdf::EvaluateSchlickNdf(cosine, alpha) * cosine * sine * step;
  }
  return 2.0F * ::Engine::Renderer::Brdf::Pi * integral;
}

auto IntegrateSawickiNdf(float alpha) -> float {
  constexpr uint32_t sampleCount = 100000;
  const float step = DirectX::XM_PIDIV2 / static_cast<float>(sampleCount);
  float integral = 0.0F;
  for (uint32_t i = 0; i < sampleCount; ++i) {
    const float theta = (static_cast<float>(i) + 0.5F) * step;
    const float cosine = std::cos(theta);
    const float sine = std::sin(theta);
    integral += ::Engine::Renderer::Brdf::EvaluateSawickiNdf(cosine, alpha) * cosine * sine * step;
  }
  return 2.0F * ::Engine::Renderer::Brdf::Pi * integral;
}

void TestSawickiReferenceAndFiniteEvaluation() {
  using ::Engine::Renderer::Brdf::EvaluateSawickiNdf;
  for (float alpha : {0.001F, 0.01F, 0.2F, 0.65F, 1.0F}) {
    Require(std::abs(IntegrateSawickiNdf(alpha) - 1.0F) < 2.0e-3F,
            "Sawicki projected NDF must normalize to one including minimum alpha");
  }
  constexpr float alpha = 0.4F;
  constexpr float noH = 0.75F;
  constexpr float nds = 4.0F * alpha - 3.0F;
  constexpr float shifted = (1.0F - nds) * noH + nds + 3.0F;
  const float expected =
      96.0F * (3.0F + nds) * noH / (::Engine::Renderer::Brdf::Pi * shifted * shifted * shifted * shifted);
  Require(std::abs(EvaluateSawickiNdf(noH, alpha) - expected) < 1.0e-6F,
          "Sawicki NDF must match its normalized rational reference equation");
  Require(EvaluateSawickiNdf(-0.1F, alpha) == 0.0F && EvaluateSawickiNdf(0.0F, alpha) == 0.0F &&
              EvaluateSawickiNdf(1.1F, alpha) == EvaluateSawickiNdf(1.0F, alpha) &&
              std::isfinite(EvaluateSawickiNdf(1.0F, 0.0F)) &&
              std::isfinite(EvaluateSawickiNdf(1.0F, std::numeric_limits<float>::quiet_NaN())),
          "Sawicki NDF must clamp its domain and stay finite at minimum roughness");
  const auto value = ::Engine::Renderer::Brdf::EvaluateSawickiBrdf({0.0F, 0.0F, 1.0F}, {0.3F, 0.0F, 0.9539392F},
                                                                   {-0.1F, 0.5F, 0.8602325F}, {0.7F, 0.4F, 0.2F},
                                                                   {0.04F, 0.04F, 0.04F}, alpha);
  Require(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z),
          "Sawicki BRDF reference evaluation must remain finite");
}

void TestSchlickReferenceAndFiniteEvaluation() {
  using ::Engine::Renderer::Brdf::EvaluateSchlickNdf;
  for (float alpha : {0.05F, 0.2F, 0.65F, 1.0F}) {
    Require(std::abs(IntegrateSchlickNdf(alpha) - 1.0F) < 2.0e-3F, "Schlick projected NDF must normalize to one");
  }
  constexpr float alpha = 0.4F;
  constexpr float noH = 0.75F;
  constexpr float m = alpha;
  constexpr float x = noH + m - 1.0F;
  constexpr float inner = (m - 1.0F) * x * x + m * m;
  const float expected = m * m * m * x / (::Engine::Renderer::Brdf::Pi * noH * inner * inner);
  Require(std::abs(EvaluateSchlickNdf(noH, alpha) - expected) < 1.0e-6F,
          "Schlick NDF must match the normalized rational reference equation");
  Require(EvaluateSchlickNdf(0.0F, alpha) == 0.0F && EvaluateSchlickNdf(0.5F, alpha) == 0.0F &&
              std::isfinite(EvaluateSchlickNdf(1.0F, 0.0F)),
          "Schlick NDF must return zero outside support and remain finite at the minimum-roughness boundary");
  const auto value = ::Engine::Renderer::Brdf::EvaluateSchlickBrdf({0.0F, 0.0F, 1.0F}, {0.3F, 0.0F, 0.9539392F},
                                                                   {-0.1F, 0.5F, 0.8602325F}, {0.7F, 0.4F, 0.2F},
                                                                   {0.04F, 0.04F, 0.04F}, alpha);
  Require(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z),
          "Schlick BRDF reference evaluation must remain finite");
}

void TestBlinnPhongReferenceAndFiniteEvaluation() {
  for (float alpha : {0.05F, 0.2F, 0.65F, 1.0F, 1.5F}) {
    Require(std::abs(IntegrateBlinnPhongNdf(alpha) - 1.0F) < 2.0e-4F,
            "Blinn-Phong projected NDF must normalize to one");
  }
  const float alpha = 0.4F;
  const float noH = 0.75F;
  const float exponent = 2.0F / (alpha * alpha) - 2.0F;
  const float expected = (exponent + 2.0F) / (2.0F * ::Engine::Renderer::Brdf::Pi) * std::pow(noH, exponent);
  Require(std::abs(::Engine::Renderer::Brdf::EvaluateBlinnPhongNdf(noH, alpha) - expected) < 1.0e-6F,
          "Blinn-Phong NDF must match the normalized exponent equation");
  Require(std::abs(::Engine::Renderer::Brdf::EvaluateBlinnPhongNdf(0.0F, 2.0F) - 1.0F / ::Engine::Renderer::Brdf::Pi) <
                  1.0e-6F &&
              ::Engine::Renderer::Brdf::EvaluateBlinnPhongNdf(0.0F, 0.4F) == 0.0F &&
              std::isfinite(::Engine::Renderer::Brdf::EvaluateBlinnPhongNdf(1.0F, 0.0F)),
          "Blinn-Phong NDF must stay finite at grazing, zero-exponent, and minimum-roughness boundaries");
  const auto value = ::Engine::Renderer::Brdf::EvaluateBlinnPhongBrdf({0.0F, 0.0F, 1.0F}, {0.3F, 0.0F, 0.9539392F},
                                                                      {-0.1F, 0.5F, 0.8602325F}, {0.7F, 0.4F, 0.2F},
                                                                      {0.04F, 0.04F, 0.04F}, alpha);
  Require(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z),
          "Blinn-Phong BRDF reference evaluation must remain finite");
}

void TestAshikhminShirleyReferenceAndFiniteEvaluation() {
  using ::Engine::Renderer::Brdf::EvaluateAshikhminShirleyNdf;
  constexpr float roughness = 0.5F;
  constexpr float exponent = 2.0F / 0.0625F - 2.0F;
  const float expectedPeak = (exponent + 1.0F) / (2.0F * ::Engine::Renderer::Brdf::Pi);
  Require(std::abs(EvaluateAshikhminShirleyNdf({0.0F, 0.0F, 1.0F}, roughness, roughness) - expectedPeak) < 1.0e-5F,
          "Ashikhmin-Shirley normal incidence must match its reference value");
  const float alongU = EvaluateAshikhminShirleyNdf({0.4F, 0.0F, 0.9165151F}, 0.2F, 0.8F);
  const float alongV = EvaluateAshikhminShirleyNdf({0.0F, 0.4F, 0.9165151F}, 0.2F, 0.8F);
  Require(std::abs(alongU - alongV) > 1.0e-3F, "Ashikhmin-Shirley must respond independently to U and V roughness");
  Require(EvaluateAshikhminShirleyNdf({1.0F, 0.0F, 0.0F}, 0.5F, 0.5F) == 0.0F &&
              std::isfinite(EvaluateAshikhminShirleyNdf({0.0F, 0.0F, 1.0F}, 0.0F, 0.0F)),
          "Ashikhmin-Shirley must remain finite at the horizon and minimum roughness");
  const auto value = ::Engine::Renderer::Brdf::EvaluateAshikhminShirleyBrdf(
      {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.3F, 0.0F, 0.9539392F}, {-0.1F, 0.5F, 0.8602325F},
      {0.7F, 0.4F, 0.2F}, {0.04F, 0.04F, 0.04F}, 0.2F, 0.8F);
  Require(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z),
          "Ashikhmin-Shirley BRDF reference evaluation must remain finite");
}

void TestGaussReferenceAndFiniteEvaluation() {
  for (float alpha : {0.05F, 0.2F, 0.65F, 1.0F}) {
    Require(std::abs(IntegrateGaussNdf(alpha) - 1.0F) < 2.0e-4F, "Gauss projected NDF must normalize to one");
  }
  const float alpha = 0.4F;
  const float noH = 0.75F;
  const float noHSquared = noH * noH;
  const float expected = std::exp(-(1.0F - noHSquared) / (2.0F * alpha * alpha * noHSquared)) /
                         (2.0F * ::Engine::Renderer::Brdf::Pi * alpha * alpha * noHSquared * noHSquared);
  Require(std::abs(::Engine::Renderer::Brdf::EvaluateGaussNdf(noH, alpha) - expected) < 1.0e-6F,
          "Gauss NDF must match its normalized Gaussian slope equation");
  Require(::Engine::Renderer::Brdf::EvaluateGaussNdf(0.0F, alpha) == 0.0F &&
              std::isfinite(::Engine::Renderer::Brdf::EvaluateGaussNdf(1.0F, 0.0F)),
          "Gauss NDF must stay finite at grazing and minimum roughness boundaries");
  const auto value = ::Engine::Renderer::Brdf::EvaluateGaussBrdf({0.0F, 0.0F, 1.0F}, {0.3F, 0.0F, 0.9539392F},
                                                                 {-0.1F, 0.5F, 0.8602325F}, {0.7F, 0.4F, 0.2F},
                                                                 {0.04F, 0.04F, 0.04F}, alpha);
  Require(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z),
          "Gauss BRDF reference evaluation must remain finite");
}

void TestBeckmannReferenceAndFiniteEvaluation() {
  for (float alpha : {0.05F, 0.2F, 0.65F, 1.0F}) {
    Require(std::abs(IntegrateBeckmannNdf(alpha) - 1.0F) < 2.0e-4F, "Beckmann projected NDF must normalize to one");
  }
  const float alpha = 0.4F;
  const float noH = 0.75F;
  const float expected = std::exp((noH * noH - 1.0F) / (alpha * alpha * noH * noH)) /
                         (::Engine::Renderer::Brdf::Pi * alpha * alpha * noH * noH * noH * noH);
  Require(std::abs(::Engine::Renderer::Brdf::EvaluateBeckmannNdf(noH, alpha) - expected) < 1.0e-6F,
          "Beckmann NDF must match its reference equation");
  Require(::Engine::Renderer::Brdf::EvaluateBeckmannNdf(0.0F, alpha) == 0.0F &&
              std::isfinite(::Engine::Renderer::Brdf::EvaluateBeckmannNdf(1.0F, 0.0F)),
          "Beckmann NDF must stay finite at grazing and minimum roughness boundaries");

  const auto value = ::Engine::Renderer::Brdf::EvaluateBeckmannBrdf({0.0F, 0.0F, 1.0F}, {0.3F, 0.0F, 0.9539392F},
                                                                    {-0.1F, 0.5F, 0.8602325F}, {0.7F, 0.4F, 0.2F},
                                                                    {0.04F, 0.04F, 0.04F}, alpha);
  Require(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z),
          "Beckmann BRDF reference evaluation must remain finite");
}

auto IntegrateWhiteFurnace(const DirectX::XMFLOAT3& view, float alpha, const DirectX::XMFLOAT3& diffuse,
                           const DirectX::XMFLOAT3& f0) -> float {
  constexpr uint32_t thetaCount = 160;
  constexpr uint32_t phiCount = 320;
  const float thetaStep = DirectX::XM_PIDIV2 / static_cast<float>(thetaCount);
  const float phiStep = 2.0F * ::Engine::Renderer::Brdf::Pi / static_cast<float>(phiCount);
  float reflectedEnergy = 0.0F;
  for (uint32_t thetaIndex = 0; thetaIndex < thetaCount; ++thetaIndex) {
    const float theta = (static_cast<float>(thetaIndex) + 0.5F) * thetaStep;
    const float sinTheta = std::sin(theta);
    const float cosTheta = std::cos(theta);
    for (uint32_t phiIndex = 0; phiIndex < phiCount; ++phiIndex) {
      const float phi = (static_cast<float>(phiIndex) + 0.5F) * phiStep;
      const DirectX::XMFLOAT3 light = {sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
      const DirectX::XMFLOAT3 value =
          ::Engine::Renderer::Brdf::EvaluateGgxBrdf({0.0F, 0.0F, 1.0F}, view, light, diffuse, f0, alpha);
      reflectedEnergy += (value.x + value.y + value.z) / 3.0F * cosTheta * sinTheta * thetaStep * phiStep;
    }
  }
  return reflectedEnergy;
}

void TestProjectedNdfNormalization() {
  for (float gamma : {1.0F, 1.5F, 2.0F, 4.0F, 8.0F}) {
    for (float alpha : {0.05F, 0.2F, 0.65F, 1.0F}) {
      const float integral = IntegrateNdf(alpha, gamma);
      Require(std::abs(integral - 1.0F) < 2.0e-4F, "GTR projected NDF must normalize to one");
    }
  }
}

void TestGtrLimitsAndRoughnessMapping() {
  for (float noH : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
    Require(std::abs(::Engine::Renderer::Brdf::EvaluateGtrNdf(noH, 0.4F, 2.0F) -
                     ::Engine::Renderer::Brdf::EvaluateGgxNdf(noH, 0.4F)) < 1.0e-6F,
            "GTR gamma=2 must be the GGX/Trowbridge-Reitz distribution");
    Require(std::abs(::Engine::Renderer::Brdf::EvaluateGtrNdf(noH, 1.0F, 1.0F) - std::numbers::inv_pi_v<float>) <
                1.0e-6F,
            "GTR alpha=1 must be uniform for gamma=1");
  }
  const float alpha = 0.35F;
  Require(std::abs(::Engine::Renderer::Brdf::EvaluateGtrNdf(1.0F, alpha, 1.0F) -
                   (::Engine::Renderer::Brdf::GtrNormalization(alpha, 1.0F) / (alpha * alpha))) < 1.0e-6F,
          "GTR gamma=1 limit must use the logarithmic normalization");
  Require(std::abs(::Engine::Renderer::Brdf::AlphaFromPerceptualRoughness(0.5F) - 0.25F) < 1.0e-6F,
          "Perceptual roughness must map to squared slope roughness");
}

void TestReciprocityAndWhiteFurnace() {
  const DirectX::XMFLOAT3 normal = {0.0F, 0.0F, 1.0F};
  const DirectX::XMFLOAT3 view = {0.3F, 0.0F, 0.9539392F};
  const DirectX::XMFLOAT3 light = {-0.1F, 0.5F, 0.8602325F};
  const DirectX::XMFLOAT3 diffuse = {0.7F, 0.4F, 0.2F};
  const DirectX::XMFLOAT3 f0 = {0.04F, 0.04F, 0.04F};
  const auto forward = ::Engine::Renderer::Brdf::EvaluateGgxBrdf(normal, view, light, diffuse, f0, 0.35F);
  // NOLINTNEXTLINE(readability-suspicious-call-argument) // Swapping view and light verifies BRDF reciprocity.
  const auto reverse = ::Engine::Renderer::Brdf::EvaluateGgxBrdf(normal, light, view, diffuse, f0, 0.35F);
  Require(std::abs(forward.x - reverse.x) < 1.0e-6F && std::abs(forward.y - reverse.y) < 1.0e-6F &&
              std::abs(forward.z - reverse.z) < 1.0e-6F,
          "GGX BRDF must satisfy reciprocity");

  for (float alpha : {0.05F, 0.2F, 0.5F, 1.0F}) {
    for (const auto& material : {
             std::pair{DirectX::XMFLOAT3{0.7F, 0.4F, 0.2F}, DirectX::XMFLOAT3{0.04F, 0.04F, 0.04F}},
             std::pair{DirectX::XMFLOAT3{0.0F, 0.0F, 0.0F}, DirectX::XMFLOAT3{0.9F, 0.7F, 0.4F}},
         }) {
      const float energy = IntegrateWhiteFurnace(view, alpha, material.first, material.second);
      Require(energy >= 0.0F && energy <= 1.001F, "Single-scatter GGX material must obey white-furnace energy bounds");
    }
  }
}

void TestMaterialGpuAbiPacking() {
  ::Engine::Renderer::MaterialProperties properties;
  properties.Model = ::Engine::Renderer::BrdfModel::GTR;
  properties.SpecularMode = ::Engine::Renderer::DielectricSpecularMode::ExplicitF0;
  properties.Roughness = 0.5F;
  properties.GtrGamma = 3.0F;
  properties.Ior = 1.7F;
  properties.SpecularF0 = {0.2F, 0.3F, 0.4F};
  const ::Engine::Renderer::MaterialGpu gpu = ::Engine::Renderer::BuildMaterialGpu(properties);

  Require(gpu.ModelFlags.x == ::Engine::Renderer::MaterialGpuAbiVersion,
          "Material GPU ABI version must be packed in the flags slot");
  Require(gpu.ModelFlags.y == static_cast<uint32_t>(::Engine::Renderer::BrdfModel::GTR),
          "Material model selector must be packed");
  Require(gpu.ModelFlags.z == static_cast<uint32_t>(::Engine::Renderer::DielectricSpecularMode::ExplicitF0),
          "Specular policy must be packed");
  Require(std::abs(gpu.SurfaceParams.x - 0.5F) < 1.0e-6F && std::abs(gpu.SurfaceParams.y - 0.25F) < 1.0e-6F,
          "Perceptual and slope roughness must be packed together");
  Require(std::abs(gpu.SurfaceParams.z - 3.0F) < 1.0e-6F && std::abs(gpu.SurfaceParams.w - 1.7F) < 1.0e-6F,
          "GTR exponent and index of refraction must be packed");
  Require(std::abs(gpu.SpecularParams.x - 0.2F) < 1.0e-6F && std::abs(gpu.SpecularParams.y - 0.3F) < 1.0e-6F &&
              std::abs(gpu.SpecularParams.z - 0.4F) < 1.0e-6F,
          "Explicit dielectric F0 must be packed");

  properties.Roughness = -1.0F;
  properties.Metallic = 2.0F;
  properties.GtrGamma = 16.0F;
  properties.Ior = 0.0F;
  const ::Engine::Renderer::MaterialGpu clamped = ::Engine::Renderer::BuildMaterialGpu(properties);
  Require(clamped.SurfaceParams.x == 0.0F && clamped.SurfaceParams.y == ::Engine::Renderer::MaterialMinAlpha &&
              clamped.SurfaceParams.z == ::Engine::Renderer::MaterialMaxGtrGamma &&
              clamped.SurfaceParams.w == ::Engine::Renderer::MaterialMinIor && clamped.EmissiveMetallic.w == 1.0F,
          "Material GPU packing must clamp model parameters to supported ranges");

  properties.Model = ::Engine::Renderer::BrdfModel::Beckmann;
  const auto beckmann = ::Engine::Renderer::BuildMaterialGpu(properties);
  Require(beckmann.ModelFlags.y == 3U, "Beckmann selector must use the appended model value without changing the ABI");
  properties.Model = ::Engine::Renderer::BrdfModel::Gauss;
  const auto gauss = ::Engine::Renderer::BuildMaterialGpu(properties);
  Require(gauss.ModelFlags.y == 4U && gauss.ModelFlags.x == ::Engine::Renderer::MaterialGpuAbiVersion,
          "Gauss selector must append value four without changing the material GPU ABI");
  properties.Model = ::Engine::Renderer::BrdfModel::BlinnPhong;
  const auto blinnPhong = ::Engine::Renderer::BuildMaterialGpu(properties);
  Require(blinnPhong.ModelFlags.y == 5U && blinnPhong.ModelFlags.x == ::Engine::Renderer::MaterialGpuAbiVersion,
          "Blinn-Phong selector must append value five without changing the material GPU ABI");
  properties.Model = ::Engine::Renderer::BrdfModel::AshikhminShirley;
  properties.AnisotropicRoughnessU = 0.2F;
  properties.AnisotropicRoughnessV = 0.8F;
  const auto ashikhmin = ::Engine::Renderer::BuildMaterialGpu(properties);
  Require(
      ashikhmin.ModelFlags.y == 6U && ashikhmin.ModelFlags.x == 5U && sizeof(ashikhmin) == 304U &&
          std::abs(ashikhmin.SpecularParams.w - 0.2F) < 1.0e-6F && std::abs(ashikhmin.AlphaParams.y - 0.8F) < 1.0e-6F,
      "Ashikhmin-Shirley selection and anisotropic values must use ABI-v5 reserved lanes without growing the buffer");
  properties.AnisotropicRoughnessU = std::numeric_limits<float>::quiet_NaN();
  properties.AnisotropicRoughnessV = 4.0F;
  const auto sanitized = ::Engine::Renderer::BuildMaterialGpu(properties);
  Require(sanitized.SpecularParams.w == 0.5F && sanitized.AlphaParams.y == 1.0F,
          "Anisotropic roughness packing must sanitize non-finite values and clamp finite values");
  properties.Model = ::Engine::Renderer::BrdfModel::Schlick;
  const auto schlick = ::Engine::Renderer::BuildMaterialGpu(properties);
  Require(schlick.ModelFlags.y == 7U && schlick.ModelFlags.x == ::Engine::Renderer::MaterialGpuAbiVersion,
          "Schlick selector must append value seven without changing the material GPU ABI");
  properties.Model = ::Engine::Renderer::BrdfModel::Sawicki;
  const auto sawicki = ::Engine::Renderer::BuildMaterialGpu(properties);
  Require(sawicki.ModelFlags.y == 8U && sawicki.ModelFlags.x == ::Engine::Renderer::MaterialGpuAbiVersion,
          "Sawicki selector must append value eight without changing the material GPU ABI");
}

void TestMaterialPropertiesRoundTrip() {
  ::Engine::Renderer::Material material(0);
  ::Engine::Renderer::MaterialProperties properties;
  properties.Model = ::Engine::Renderer::BrdfModel::GTR;
  properties.AnisotropicRoughnessU = 0.23F;
  properties.AnisotropicRoughnessV = 0.79F;
  properties.SpecularMode = ::Engine::Renderer::DielectricSpecularMode::ExplicitF0;
  properties.GtrGamma = 4.0F;
  properties.Ior = 1.8F;
  properties.SpecularF0 = {0.12F, 0.23F, 0.34F};
  material.SetProperties(properties);
  const auto restored = material.GetProperties();
  Require(restored.Model == properties.Model && restored.SpecularMode == properties.SpecularMode &&
              std::abs(restored.GtrGamma - properties.GtrGamma) < 1.0e-6F &&
              std::abs(restored.Ior - properties.Ior) < 1.0e-6F &&
              std::abs(restored.SpecularF0.z - properties.SpecularF0.z) < 1.0e-6F &&
              std::abs(restored.AnisotropicRoughnessU - properties.AnisotropicRoughnessU) < 1.0e-6F &&
              std::abs(restored.AnisotropicRoughnessV - properties.AnisotropicRoughnessV) < 1.0e-6F,
          "Material must retain authored BRDF model parameters");
}

void TestRenderViewUsesExplicitCameraView() {
  ::Engine::Scene scene;
  Engine::Viewport viewport(320, 200, nullptr);
  ::Engine::Renderer::CameraView camera;
  camera.Position = {3.0F, 4.0F, 5.0F};
  ::Engine::Renderer::RenderView view;
  view.Initialize(&scene, camera, &viewport);
  Require(std::abs(view.CameraPosition.x - 3.0F) < 1.0e-6F && std::abs(view.CameraPosition.y - 4.0F) < 1.0e-6F &&
              std::abs(view.CameraPosition.z - 5.0F) < 1.0e-6F && view.ViewportWidth == 320 &&
              view.ViewportHeight == 200,
          "Render views must use explicit camera pose without requiring a scene camera entity");
}

void TestEcsTransformComponentValidation() {
  ::Engine::Ecs::LocalTransform transform;
  Require(::Engine::Ecs::Validate(transform).has_value(), "Default ECS transforms must satisfy component invariants");
  transform.Scale[1] = 0.0F;
  Require(!::Engine::Ecs::Validate(transform).has_value(), "ECS transforms must reject non-positive scale");
}
} // namespace

auto main() -> int {
  try {
    TestProjectedNdfNormalization();
    TestSawickiReferenceAndFiniteEvaluation();
    TestBeckmannReferenceAndFiniteEvaluation();
    TestGaussReferenceAndFiniteEvaluation();
    TestBlinnPhongReferenceAndFiniteEvaluation();
    TestAshikhminShirleyReferenceAndFiniteEvaluation();
    TestSchlickReferenceAndFiniteEvaluation();
    TestGtrLimitsAndRoughnessMapping();
    TestReciprocityAndWhiteFurnace();
    TestMaterialGpuAbiPacking();
    TestMaterialPropertiesRoundTrip();
    TestRenderViewUsesExplicitCameraView();
    TestEcsTransformComponentValidation();
    std::cout << "BRDF reference tests passed\n";
    return EXIT_SUCCESS;
  } catch (...) {
    std::fputs("BRDF reference tests failed\n", stderr);
    return EXIT_FAILURE;
  }
}
