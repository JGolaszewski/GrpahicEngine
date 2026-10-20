#include "EnvironmentLighting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace Engine::Renderer {
namespace {
constexpr float Pi = std::numbers::pi_v<float>;
constexpr uint32_t EnvironmentWidth = 64;
constexpr uint32_t EnvironmentHeight = 32;
constexpr uint32_t IrradianceWidth = 32;
constexpr uint32_t IrradianceHeight = 16;
constexpr uint32_t BrdfLutSize = 64;
constexpr uint32_t IntegrationSamples = 96;

struct Vec3 {
  float X;
  float Y;
  float Z;
};

struct FloatMip {
  uint32_t Width;
  uint32_t Height;
  std::vector<float> Pixels;
};

auto Add(Vec3 a, Vec3 b) -> Vec3 { return {a.X + b.X, a.Y + b.Y, a.Z + b.Z}; }
auto Scale(Vec3 a, float scale) -> Vec3 { return {a.X * scale, a.Y * scale, a.Z * scale}; }
auto Dot(Vec3 a, Vec3 b) -> float { return (a.X * b.X) + (a.Y * b.Y) + (a.Z * b.Z); }
auto Normalize(Vec3 value) -> Vec3 {
  const float lengthSquared = Dot(value, value);
  if (lengthSquared <= 1.0e-12F) {
    return {0.0F, 1.0F, 0.0F};
  }
  return Scale(value, 1.0F / std::sqrt(lengthSquared));
}
auto Cross(Vec3 a, Vec3 b) -> Vec3 {
  return {(a.Y * b.Z) - (a.Z * b.Y), (a.Z * b.X) - (a.X * b.Z), (a.X * b.Y) - (a.Y * b.X)};
}
auto Mix(Vec3 a, Vec3 b, float amount) -> Vec3 { return Add(Scale(a, 1.0F - amount), Scale(b, amount)); }

auto DirectionFromUv(float u, float v) -> Vec3 {
  const float longitude = (u - 0.5F) * 2.0F * Pi;
  const float latitude = v * Pi;
  const float sinLatitude = std::sin(latitude);
  return {std::cos(longitude) * sinLatitude, std::cos(latitude), std::sin(longitude) * sinLatitude};
}

auto StudioRadiance(Vec3 direction) -> Vec3 {
  const float skyAmount = std::clamp(direction.Y * 0.5F + 0.5F, 0.0F, 1.0F);
  const Vec3 horizon = direction.Y >= 0.0F ? Vec3{0.34F, 0.42F, 0.56F} : Vec3{0.12F, 0.105F, 0.09F};
  const Vec3 zenith = direction.Y >= 0.0F ? Vec3{0.08F, 0.18F, 0.38F} : Vec3{0.025F, 0.022F, 0.02F};
  Vec3 radiance = Mix(horizon, zenith, direction.Y >= 0.0F ? skyAmount : 1.0F - skyAmount);

  const Vec3 sunDirection = Normalize({-0.45F, 0.72F, -0.52F});
  const float sun = std::pow(std::clamp(Dot(direction, sunDirection), 0.0F, 1.0F), 320.0F);
  radiance = Add(radiance, Scale({1.0F, 0.72F, 0.42F}, 18.0F * sun));

  const Vec3 softboxDirection = Normalize({0.55F, 0.42F, 0.72F});
  const float softbox = std::pow(std::clamp(Dot(direction, softboxDirection), 0.0F, 1.0F), 24.0F);
  return Add(radiance, Scale({0.72F, 0.82F, 1.0F}, 2.4F * softbox));
}

auto RadicalInverse(uint32_t bits) -> float {
  bits = (bits << 16U) | (bits >> 16U);
  bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
  bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
  bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
  bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
  return static_cast<float>(bits) * 2.3283064365386963e-10F;
}

auto Hammersley(uint32_t index) -> std::array<float, 2> {
  return {static_cast<float>(index) / static_cast<float>(IntegrationSamples), RadicalInverse(index)};
}

auto SampleStudio(float u, float v) -> Vec3 { return StudioRadiance(DirectionFromUv(u, v)); }

auto ImportanceSampleCosine(const std::array<float, 2>& xi, Vec3 normal) -> Vec3 {
  const float radius = std::sqrt(xi[0]);
  const float phi = 2.0F * Pi * xi[1];
  const Vec3 local{radius * std::cos(phi), radius * std::sin(phi), std::sqrt(1.0F - xi[0])};
  const Vec3 up = std::abs(normal.Y) < 0.999F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
  const Vec3 tangent = Normalize(Cross(up, normal));
  const Vec3 bitangent = Cross(normal, tangent);
  return Normalize(Add(Add(Scale(tangent, local.X), Scale(bitangent, local.Y)), Scale(normal, local.Z)));
}

auto ImportanceSampleGgx(const std::array<float, 2>& xi, Vec3 normal, float roughness) -> Vec3 {
  const float alpha = (std::max)(roughness * roughness, 1.0e-3F);
  const float alphaSquared = alpha * alpha;
  const float phi = 2.0F * Pi * xi[0];
  const float cosTheta = std::sqrt((1.0F - xi[1]) / (1.0F + ((alphaSquared - 1.0F) * xi[1])));
  const float sinTheta = std::sqrt((std::max)(0.0F, 1.0F - (cosTheta * cosTheta)));
  const Vec3 halfVectorLocal{std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta};
  const Vec3 up = std::abs(normal.Y) < 0.999F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
  const Vec3 tangent = Normalize(Cross(up, normal));
  const Vec3 bitangent = Cross(normal, tangent);
  return Normalize(Add(Add(Scale(tangent, halfVectorLocal.X), Scale(bitangent, halfVectorLocal.Y)),
                       Scale(normal, halfVectorLocal.Z)));
}

auto IntegrateIrradiance(Vec3 normal) -> Vec3 {
  Vec3 result{0.0F, 0.0F, 0.0F};
  for (uint32_t sample = 0; sample < IntegrationSamples; ++sample) {
    const Vec3 direction = ImportanceSampleCosine(Hammersley(sample), normal);
    const float u = std::atan2(direction.Z, direction.X) / (2.0F * Pi) + 0.5F;
    const float v = std::acos(std::clamp(direction.Y, -1.0F, 1.0F)) / Pi;
    result = Add(result, SampleStudio(u, v));
  }
  return Scale(result, Pi / static_cast<float>(IntegrationSamples));
}

auto IntegratePrefilter(Vec3 normal, float roughness) -> Vec3 {
  const Vec3 view = normal;
  Vec3 result{0.0F, 0.0F, 0.0F};
  float totalWeight = 0.0F;
  for (uint32_t sample = 0; sample < IntegrationSamples; ++sample) {
    const Vec3 halfVector = ImportanceSampleGgx(Hammersley(sample), normal, roughness);
    const Vec3 light = Normalize(Add(Scale(halfVector, 2.0F * Dot(view, halfVector)), Scale(view, -1.0F)));
    const float noL = (std::max)(Dot(normal, light), 0.0F);
    if (noL > 0.0F) {
      const float u = std::atan2(light.Z, light.X) / (2.0F * Pi) + 0.5F;
      const float v = std::acos(std::clamp(light.Y, -1.0F, 1.0F)) / Pi;
      result = Add(result, Scale(SampleStudio(u, v), noL));
      totalWeight += noL;
    }
  }
  return (totalWeight > 0.0F) ? Scale(result, 1.0F / totalWeight) : SampleStudio(0.5F, 0.5F);
}

auto GeometrySmithGgx(float noV, float noL, float roughness) -> float {
  const float alpha = roughness * roughness;
  const float alphaSquared = alpha * alpha;
  const auto geometryOne = [alphaSquared](float noX) -> float {
    const float denominator = noX + std::sqrt((alphaSquared + ((1.0F - alphaSquared) * noX * noX)));
    return denominator > 1.0e-6F ? (2.0F * noX / denominator) : 0.0F;
  };
  return geometryOne(noV) * geometryOne(noL);
}

auto IntegrateBrdf(float noV, float roughness) -> std::array<float, 2> {
  const Vec3 normal{0.0F, 0.0F, 1.0F};
  const Vec3 view{std::sqrt((std::max)(0.0F, 1.0F - noV * noV)), 0.0F, noV};
  float a = 0.0F;
  float b = 0.0F;
  for (uint32_t sample = 0; sample < IntegrationSamples; ++sample) {
    const Vec3 halfVector = ImportanceSampleGgx(Hammersley(sample), normal, roughness);
    const Vec3 light = Normalize(Add(Scale(halfVector, 2.0F * Dot(view, halfVector)), Scale(view, -1.0F)));
    const float noL = (std::max)(light.Z, 0.0F);
    const float noH = (std::max)(halfVector.Z, 0.0F);
    const float voH = (std::max)(Dot(view, halfVector), 0.0F);
    if (noL > 0.0F && noH > 0.0F) {
      const float visibility = (GeometrySmithGgx(noV, noL, roughness) * voH) / (noH * noV);
      const float fresnel = std::pow(1.0F - voH, 5.0F);
      a += (1.0F - fresnel) * visibility;
      b += fresnel * visibility;
    }
  }
  return {a / static_cast<float>(IntegrationSamples), b / static_cast<float>(IntegrationSamples)};
}

void StorePixel(FloatMip& mip, uint32_t x, uint32_t y, Vec3 color, float alpha = 1.0F) {
  const size_t offset = (static_cast<size_t>(y) * mip.Width + x) * 4;
  mip.Pixels.at(offset) = color.X;
  mip.Pixels.at(offset + 1) = color.Y;
  mip.Pixels.at(offset + 2) = color.Z;
  mip.Pixels.at(offset + 3) = alpha;
}

auto MakeIrradianceMip() -> FloatMip {
  FloatMip mip{IrradianceWidth, IrradianceHeight,
               std::vector<float>(static_cast<size_t>(IrradianceWidth) * IrradianceHeight * 4)};
  for (uint32_t y = 0; y < mip.Height; ++y) {
    for (uint32_t x = 0; x < mip.Width; ++x) {
      const Vec3 normal = DirectionFromUv((static_cast<float>(x) + 0.5F) / static_cast<float>(mip.Width),
                                          (static_cast<float>(y) + 0.5F) / static_cast<float>(mip.Height));
      StorePixel(mip, x, y, IntegrateIrradiance(normal));
    }
  }
  return mip;
}

auto MakePrefilterMips() -> std::vector<FloatMip> {
  std::vector<FloatMip> mips;
  uint32_t width = EnvironmentWidth;
  uint32_t height = EnvironmentHeight;
  while (true) {
    const float roughness = mips.empty() ? 0.0F : static_cast<float>(mips.size()) / 6.0F;
    FloatMip mip{width, height, std::vector<float>(static_cast<size_t>(width) * height * 4)};
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        const Vec3 direction = DirectionFromUv((static_cast<float>(x) + 0.5F) / static_cast<float>(width),
                                               (static_cast<float>(y) + 0.5F) / static_cast<float>(height));
        StorePixel(mip, x, y,
                   roughness <= 0.001F ? StudioRadiance(direction) : IntegratePrefilter(direction, roughness));
      }
    }
    mips.push_back(std::move(mip));
    if (width == 1 && height == 1) {
      break;
    }
    width = (std::max)(1U, width / 2U);
    height = (std::max)(1U, height / 2U);
  }
  return mips;
}

auto MakeBrdfLut() -> FloatMip {
  FloatMip mip{BrdfLutSize, BrdfLutSize, std::vector<float>(static_cast<size_t>(BrdfLutSize) * BrdfLutSize * 4)};
  for (uint32_t y = 0; y < mip.Height; ++y) {
    for (uint32_t x = 0; x < mip.Width; ++x) {
      const float noV = (static_cast<float>(x) + 0.5F) / static_cast<float>(mip.Width);
      const float roughness = (static_cast<float>(y) + 0.5F) / static_cast<float>(mip.Height);
      const auto integrated = IntegrateBrdf((std::max)(noV, 0.001F), roughness);
      StorePixel(mip, x, y, {integrated[0], integrated[1], 0.0F});
    }
  }
  return mip;
}

auto CreateTexture(nvrhi::IDevice* device, const std::vector<FloatMip>& mips, const char* name)
    -> nvrhi::TextureHandle {
  if (device == nullptr || mips.empty()) {
    return nullptr;
  }
  nvrhi::TextureDesc desc;
  desc.setDimension(nvrhi::TextureDimension::Texture2D)
      .setWidth(mips.front().Width)
      .setHeight(mips.front().Height)
      .setArraySize(1)
      .setMipLevels(static_cast<uint32_t>(mips.size()))
      .setFormat(nvrhi::Format::RGBA32_FLOAT)
      .setDebugName(name)
      .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
  nvrhi::TextureHandle texture = device->createTexture(desc);
  if (texture == nullptr) {
    return nullptr;
  }
  return texture;
}

auto CreateSampler(nvrhi::IDevice* device, bool mipFilter) -> nvrhi::SamplerHandle {
  if (device == nullptr) {
    return nullptr;
  }
  nvrhi::SamplerDesc desc;
  desc.setAddressU(nvrhi::SamplerAddressMode::Wrap)
      .setAddressV(nvrhi::SamplerAddressMode::Clamp)
      .setAddressW(nvrhi::SamplerAddressMode::Clamp)
      .setMinFilter(true)
      .setMagFilter(true)
      .setMipFilter(mipFilter);
  return device->createSampler(desc);
}
} // namespace

bool EnvironmentLightingResources::Initialize(nvrhi::IDevice* device, std::string& error) {
  error.clear();
  if (device == nullptr) {
    error = "environment resource creation requires a valid NVRHI device";
    return false;
  }

  const FloatMip irradiance = MakeIrradianceMip();
  const std::vector<FloatMip> prefilteredMips = MakePrefilterMips();
  const FloatMip brdfLut = MakeBrdfLut();
  IrradianceMap = CreateTexture(device, {irradiance}, "StudioEnvironmentIrradiance");
  PrefilteredEnvironment = CreateTexture(device, prefilteredMips, "StudioEnvironmentPrefiltered");
  BrdfIntegrationLut = CreateTexture(device, {brdfLut}, "StudioEnvironmentBrdfLut");
  IrradianceSampler = CreateSampler(device, false);
  PrefilteredEnvironmentSampler = CreateSampler(device, true);
  BrdfIntegrationSampler = CreateSampler(device, false);
  if (IrradianceMap == nullptr || PrefilteredEnvironment == nullptr || BrdfIntegrationLut == nullptr ||
      IrradianceSampler == nullptr || PrefilteredEnvironmentSampler == nullptr || BrdfIntegrationSampler == nullptr) {
    error = "NVRHI failed to create one or more Studio environment textures or samplers";
    return false;
  }

  nvrhi::CommandListHandle commandList = device->createCommandList();
  if (commandList == nullptr) {
    error = "NVRHI failed to create the Studio environment upload command list";
    return false;
  }
  commandList->open();
  commandList->writeTexture(IrradianceMap, 0, 0, irradiance.Pixels.data(),
                            static_cast<size_t>(IrradianceWidth) * 4 * sizeof(float), 0);
  for (size_t mipIndex = 0; mipIndex < prefilteredMips.size(); ++mipIndex) {
    const FloatMip& mip = prefilteredMips.at(mipIndex);
    commandList->writeTexture(PrefilteredEnvironment, 0, static_cast<uint32_t>(mipIndex), mip.Pixels.data(),
                              static_cast<size_t>(mip.Width) * 4 * sizeof(float), 0);
  }
  commandList->writeTexture(BrdfIntegrationLut, 0, 0, brdfLut.Pixels.data(),
                            static_cast<size_t>(BrdfLutSize) * 4 * sizeof(float), 0);
  commandList->close();
  device->executeCommandList(commandList);
  device->waitForIdle();
  return true;
}

} // namespace Engine::Renderer
