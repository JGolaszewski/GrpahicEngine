#include "../../support/WarpRenderBackend.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/src/MaterialBackend.h"
#include "rendering/src/MaterialSystem.h"
#include "rendering/src/TextureBackend.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using Engine::Renderer::ResolveTextureAnisotropy;
using Engine::Renderer::TextureAnisotropy;
using Engine::Renderer::TextureBackendAccess;

namespace {
auto CreateTestTexture(nvrhi::IDevice* device, const std::string& name)
    -> std::shared_ptr<Engine::Renderer::TextureResource> {
  auto resource = std::make_shared<Engine::Renderer::TextureResource>();
  nvrhi::TextureDesc textureDesc;
  textureDesc.setDimension(nvrhi::TextureDimension::Texture2D)
      .setWidth(1)
      .setHeight(1)
      .setFormat(nvrhi::Format::RGBA8_UNORM)
      .setDebugName(name)
      .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
  auto texture = device->createTexture(textureDesc);
  if (texture == nullptr) {
    return nullptr;
  }

  nvrhi::SamplerDesc samplerDesc;
  samplerDesc.setAddressU(nvrhi::SamplerAddressMode::Mirror)
      .setAddressV(nvrhi::SamplerAddressMode::Wrap)
      .setMinFilter(false)
      .setMagFilter(false)
      .setMipFilter(false);
  auto sampler = device->createSampler(samplerDesc);
  if (sampler == nullptr) {
    return nullptr;
  }

  TextureBackendAccess::SetTexture(*resource, std::move(texture));
  TextureBackendAccess::SetSamplerDesc(*resource, samplerDesc);
  TextureBackendAccess::SetSampler(*resource, std::move(sampler));
  return resource;
}

auto GetSamplerAtSlot(const Engine::Renderer::Material& material, uint32_t slot) -> nvrhi::ISampler* {
  const auto bindingSet = Engine::Renderer::MaterialBackendAccess::GetBindingSet(material);
  if (!bindingSet || bindingSet->getDesc() == nullptr) {
    return nullptr;
  }
  for (const auto& binding : bindingSet->getDesc()->bindings) {
    if (binding.type == nvrhi::ResourceType::Sampler && binding.slot == slot) {
      return static_cast<nvrhi::ISampler*>(binding.resourceHandle);
    }
  }
  return nullptr;
}

auto UpdateMaterials(Engine::Renderer::MaterialSystem& materialSystem, nvrhi::IDevice* device) -> bool {
  auto commandList = device->createCommandList();
  if (commandList == nullptr) {
    return false;
  }
  commandList->open();
  materialSystem.UpdateDirtyMaterials(commandList);
  commandList->close();
  device->executeCommandList(commandList);
  return true;
}
} // namespace

TEST(TextureAnisotropyTests, GlobalCapResolvesToEngineMaximum) {
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::GlobalCap, 8), 8U);
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::GlobalCap, 16), 16U);
}

TEST(TextureAnisotropyTests, ExplicitRequestIsClampedToEngineMaximum) {
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::X16, 8), 8U);
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::X4, 8), 4U);
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::X2, 1), 1U);
}

TEST(TextureAnisotropyTests, InvalidValuesFallBackToEngineMaximum) {
  EXPECT_EQ(ResolveTextureAnisotropy(static_cast<TextureAnisotropy>(3), 8), 8U);
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::X4, 3), 4U);
}

TEST(TextureAnisotropyTests, WarpSamplerCacheAndGlobalChangesRefreshExistingAndFutureMaterials) {
  EngineTests::WarpDevice warp;
  if (!warp.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }
  nvrhi::IDevice* const device = warp.GetDevice();
  auto fallbackColor = CreateTestTexture(device, "AnisotropyTest_Color");
  auto fallbackNormal = CreateTestTexture(device, "AnisotropyTest_Normal");
  ASSERT_NE(fallbackColor, nullptr);
  ASSERT_NE(fallbackNormal, nullptr);

  const auto baseSampler = TextureBackendAccess::GetSampler(*fallbackColor);
  ASSERT_NE(baseSampler, nullptr);
  EXPECT_FALSE(baseSampler->getDesc().minFilter);
  EXPECT_FALSE(baseSampler->getDesc().magFilter);
  EXPECT_FALSE(baseSampler->getDesc().mipFilter);
  const auto anisotropicEight = TextureBackendAccess::GetSampler(*fallbackColor, 8, device);
  ASSERT_NE(anisotropicEight, nullptr);
  EXPECT_FLOAT_EQ(anisotropicEight->getDesc().maxAnisotropy, 8.0F);
  EXPECT_TRUE(anisotropicEight->getDesc().minFilter);
  EXPECT_TRUE(anisotropicEight->getDesc().magFilter);
  EXPECT_TRUE(anisotropicEight->getDesc().mipFilter);
  EXPECT_EQ(anisotropicEight->getDesc().addressU, nvrhi::SamplerAddressMode::Mirror);
  EXPECT_EQ(anisotropicEight->getDesc().addressV, nvrhi::SamplerAddressMode::Wrap);
  EXPECT_EQ(TextureBackendAccess::GetSampler(*fallbackColor, 1, device), baseSampler);
  EXPECT_EQ(TextureBackendAccess::GetSampler(*fallbackColor, 8, device), anisotropicEight);

  Engine::Renderer::MaterialProperties properties;
  properties.TextureAnisotropyLevels[0] = TextureAnisotropy::X16;
  Engine::Renderer::MaterialSystem materialSystem(device, {}, {}, fallbackColor, fallbackNormal);
  auto existing = materialSystem.CreateMaterial(properties);
  ASSERT_NE(existing, nullptr);
  ASSERT_NE(GetSamplerAtSlot(*existing, 0), nullptr);
  EXPECT_FLOAT_EQ(GetSamplerAtSlot(*existing, 0)->getDesc().maxAnisotropy, 8.0F);

  materialSystem.SetMaxTextureAnisotropy(4);
  EXPECT_TRUE(existing->IsDirty());
  ASSERT_TRUE(UpdateMaterials(materialSystem, device));
  EXPECT_FALSE(existing->IsDirty());
  auto* const refreshedSampler = GetSamplerAtSlot(*existing, 0);
  ASSERT_NE(refreshedSampler, nullptr);
  EXPECT_FLOAT_EQ(refreshedSampler->getDesc().maxAnisotropy, 4.0F);
  EXPECT_EQ(refreshedSampler, TextureBackendAccess::GetSampler(*fallbackColor, 4, device).Get());

  auto future = materialSystem.CreateMaterial(properties);
  ASSERT_NE(future, nullptr);
  auto* const futureSampler = GetSamplerAtSlot(*future, 0);
  ASSERT_NE(futureSampler, nullptr);
  EXPECT_FLOAT_EQ(futureSampler->getDesc().maxAnisotropy, 4.0F);
}
