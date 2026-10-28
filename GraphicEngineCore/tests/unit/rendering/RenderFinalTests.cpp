#include "assets/include/AssetManager.h"
#include "panels/include/AssetDragDropPayload.h"
#include "panels/include/AssetDropAssignment.h"
#include "rendering/include/RenderSystem.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/include/renderer/brdf/Brdf.h"
#include "rendering/src/SceneRenderer.h"
#include "rendering/src/render_graph/RenderGraph.h"
#include "rendering/src/render_graph/RenderView.h"
#include "scene/include/Scene.h"
#include "scene/include/components/EcsComponents.h"

#include "../../support/WarpRenderBackend.h"

#include <gtest/gtest.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {
auto ReadViewportPixel(nvrhi::IDevice* device, Engine::Viewport& viewport, uint32_t x, uint32_t y)
    -> std::array<uint8_t, 4> {
  auto* framebuffer = static_cast<nvrhi::IFramebuffer*>(viewport.GetFramebufferHandle());
  if (device == nullptr || framebuffer == nullptr || x >= viewport.GetWidth() || y >= viewport.GetHeight() ||
      framebuffer->getDesc().colorAttachments.empty()) {
    return {};
  }
  auto* source = framebuffer->getDesc().colorAttachments.front().texture;
  if (source == nullptr) {
    return {};
  }
  const nvrhi::TextureDesc sourceDesc = source->getDesc();
  nvrhi::TextureDesc stagingDesc;
  stagingDesc.width = sourceDesc.width;
  stagingDesc.height = sourceDesc.height;
  stagingDesc.format = sourceDesc.format;
  stagingDesc.dimension = sourceDesc.dimension;
  auto staging = device->createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);
  if (staging == nullptr) {
    return {};
  }
  auto commandList = device->createCommandList();
  if (commandList == nullptr) {
    return {};
  }
  commandList->open();
  commandList->setTextureState(source, nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource);
  commandList->commitBarriers();
  commandList->copyTexture(staging, nvrhi::TextureSlice(), source, nvrhi::TextureSlice());
  commandList->setTextureState(source, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
  commandList->commitBarriers();
  commandList->close();
  device->executeCommandList(commandList);
  device->waitForIdle();
  size_t rowPitch = 0;
  auto* mapped = static_cast<const uint8_t*>(
      device->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
  if (mapped == nullptr) {
    return {};
  }
  std::array<uint8_t, 4> pixel{};
  std::copy_n(mapped + static_cast<size_t>(y) * rowPitch + static_cast<size_t>(x) * 4U, pixel.size(), pixel.begin());
  device->unmapStagingTexture(staging);
  return pixel;
}

auto LinearToDisplay(float linear) -> float {
  const float reinhard = std::max(linear, 0.0F) / (1.0F + std::max(linear, 0.0F));
  return reinhard <= 0.0031308F ? 12.92F * reinhard : 1.055F * std::pow(reinhard, 1.0F / 2.4F) - 0.055F;
}

void WriteSolidHdr(const std::filesystem::path& path) {
  constexpr std::array<uint8_t, 8> pixels = {128, 64, 32, 130, 128, 64, 32, 130};
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // Radiance fixture stores encoded RGBE bytes.
  output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
}

void WriteConstantHdr(const std::filesystem::path& path, uint8_t red) {
  const std::array<uint8_t, 8> pixels = {red, static_cast<uint8_t>(red / 2U), static_cast<uint8_t>(red / 4U), 130,
                                         red, static_cast<uint8_t>(red / 2U), static_cast<uint8_t>(red / 4U), 130};
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // Radiance fixture stores encoded RGBE bytes.
  output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
}

auto HighSampleConstantEnvironmentReference(const std::array<double, 3>& radiance, double normalization, double f0,
                                            double roughness, uint32_t sampleCount) -> std::array<double, 3> {
  const double alpha = roughness * roughness;
  const double alphaSquared = alpha * alpha;
  std::array<double, 3> result{};
  for (uint32_t index = 0; index < sampleCount; ++index) {
    const double radiusSquared = (static_cast<double>(index) + 0.5) / sampleCount;
    const double noH = std::sqrt(1.0 - radiusSquared);
    const double noL = 1.0 - 2.0 * radiusSquared;
    if (noL <= 1.0e-6) {
      continue;
    }
    const double smithV = noL * std::sqrt(alphaSquared + (1.0 - alphaSquared));
    const double smithL = std::sqrt(alphaSquared + (1.0 - alphaSquared) * noL * noL);
    const double geometry = (2.0 * noL) / (smithV + smithL);
    const double fresnel = f0 + (1.0 - f0) * std::pow(1.0 - noH, 5.0);
    const double contribution = normalization * geometry * fresnel;
    for (size_t channel = 0; channel < result.size(); ++channel) {
      result[channel] += radiance[channel] * contribution;
    }
  }
  for (double& channel : result) {
    channel *= std::numbers::pi / sampleCount;
  }
  return result;
}

auto CenterSpecular(const Engine::RenderOutput& output, uint32_t width, uint32_t height) -> std::array<float, 3> {
  const std::span<const float> pixels = output.GetPixels(Engine::RenderAov::SpecularLighting);
  const size_t pixelOffset = (static_cast<size_t>(height / 2U) * width + width / 2U) * 3U;
  if (pixels.size() < pixelOffset + 3U) {
    return {};
  }
  return {pixels[pixelOffset], pixels[pixelOffset + 1U], pixels[pixelOffset + 2U]};
}

void WriteSolidPng(const std::filesystem::path& path) {
  constexpr std::array<uint8_t, 8> pixels = {200, 50, 20, 255, 200, 50, 20, 255};
  ASSERT_NE(stbi_write_png(path.string().c_str(), 2, 1, 4, pixels.data(), 8), 0);
}

TEST(RenderFinalTests, WARPSceneSkyboxRendersAsBackgroundInAllViewModes) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }

  const auto project =
      std::filesystem::temp_directory_path() /
      ("GraphicEngineSkybox-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(project);
  WriteSolidHdr(project / "sky.hdr");
  Engine::AssetManager assets(project);
  const auto environmentGuid = assets.ImportEnvironment("sky.hdr");
  ASSERT_TRUE(environmentGuid.has_value()) << (environmentGuid.has_value() ? "" : environmentGuid.error().Message);

  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());
  renderer.SetAssetManager(&assets);
  std::string error;
  ASSERT_TRUE(renderer.PrepareAssetEnvironment(*environmentGuid, error)) << error;

  Engine::Scene scene;
  const uint64_t skyboxSequence =
      scene.QueueAdd(scene.SceneRootEntity(), Engine::Ecs::Skybox{.EnvironmentGuid = *environmentGuid});
  const auto skyboxResult = scene.sync();
  ASSERT_EQ(skyboxResult.size(), 1U);
  ASSERT_EQ(skyboxResult.front().Sequence, skyboxSequence);
  ASSERT_TRUE(skyboxResult.front().Outcome);

  const auto material = renderer.CreateMaterial(Engine::Renderer::MaterialProperties{});
  ASSERT_NE(material, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Cube, material));
  const uint64_t entitySequence = scene.QueueCreate("Skybox foreground object");
  const auto entityResult = scene.sync();
  ASSERT_EQ(entityResult.size(), 1U);
  ASSERT_EQ(entityResult.front().Sequence, entitySequence);
  ASSERT_TRUE(entityResult.front().Outcome);
  (void)scene.QueueAdd(entityResult.front().Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "cube"});
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -5.0F};
  constexpr uint32_t imageSize = 32;
  const auto finalOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(finalOutput.has_value()) << (finalOutput.has_value() ? "" : finalOutput.error().Message);
  const auto finalColor = finalOutput->GetPixels(Engine::RenderAov::FinalColor);
  const auto depth = finalOutput->GetPixels(Engine::RenderAov::Depth);
  const size_t backgroundPixel = (static_cast<size_t>(2) * imageSize + 2U) * 4U;
  const size_t centerDepth = static_cast<size_t>(imageSize / 2U) * imageSize + imageSize / 2U;
  ASSERT_GT(finalColor.size(), backgroundPixel + 3U);
  ASSERT_GT(depth.size(), centerDepth);
  EXPECT_GT(finalColor[backgroundPixel], 0.1F);
  EXPECT_GT(finalColor[backgroundPixel + 1U], 0.05F);
  EXPECT_GT(finalColor[backgroundPixel + 2U], 0.01F);
  EXPECT_FLOAT_EQ(finalColor[backgroundPixel + 3U], 1.0F);
  EXPECT_FLOAT_EQ(depth[0], 1.0F);
  EXPECT_LT(depth[centerDepth], depth[0]);
  const auto authoredSpecular = finalOutput->GetPixels(Engine::RenderAov::SpecularLighting);
  const size_t centerSpecular = centerDepth * 3U;
  ASSERT_GT(authoredSpecular.size(), centerSpecular + 2U);
  const float authoredSpecularEnergy =
      authoredSpecular[centerSpecular] + authoredSpecular[centerSpecular + 1U] + authoredSpecular[centerSpecular + 2U];
  EXPECT_GT(authoredSpecularEnergy, 0.0F);

  (void)scene.QueueSet(scene.SceneRootEntity(), Engine::Ecs::Skybox{.Intensity = 0.0F});
  ASSERT_TRUE(scene.Update(0.0F));
  const auto emptyEnvironment = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(emptyEnvironment.has_value()) << (emptyEnvironment.has_value() ? "" : emptyEnvironment.error().Message);
  const auto emptySpecular = emptyEnvironment->GetPixels(Engine::RenderAov::SpecularLighting);
  ASSERT_GT(emptySpecular.size(), centerSpecular + 2U);
  EXPECT_NEAR(emptySpecular[centerSpecular] + emptySpecular[centerSpecular + 1U] + emptySpecular[centerSpecular + 2U],
              0.0F, 1.0e-4F);

  (void)scene.QueueSet(scene.SceneRootEntity(), Engine::Ecs::Skybox{.EnvironmentGuid = *environmentGuid});
  ASSERT_TRUE(scene.Update(0.0F));
  (void)scene.QueueSet(
      scene.SceneRootEntity(),
      Engine::Ecs::Skybox{.EnvironmentGuid = *environmentGuid, .EnableEnvironmentLighting = false, .Intensity = 1.0F});
  ASSERT_TRUE(scene.Update(0.0F));
  const auto disabledEnvironment = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(disabledEnvironment.has_value())
      << (disabledEnvironment.has_value() ? "" : disabledEnvironment.error().Message);
  const auto disabledSpecular = disabledEnvironment->GetPixels(Engine::RenderAov::SpecularLighting);
  ASSERT_GT(disabledSpecular.size(), centerSpecular + 2U);
  EXPECT_NEAR(disabledSpecular[centerSpecular] + disabledSpecular[centerSpecular + 1U] +
                  disabledSpecular[centerSpecular + 2U],
              0.0F, 1.0e-4F);

  (void)scene.QueueSet(
      scene.SceneRootEntity(),
      Engine::Ecs::Skybox{.EnvironmentGuid = *environmentGuid, .EnableEnvironmentLighting = true, .Intensity = 0.0F});
  ASSERT_TRUE(scene.Update(0.0F));
  const auto zeroIntensity = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(zeroIntensity.has_value()) << (zeroIntensity.has_value() ? "" : zeroIntensity.error().Message);
  const auto zeroSpecular = zeroIntensity->GetPixels(Engine::RenderAov::SpecularLighting);
  ASSERT_GT(zeroSpecular.size(), centerSpecular + 2U);
  EXPECT_NEAR(zeroSpecular[centerSpecular] + zeroSpecular[centerSpecular + 1U] + zeroSpecular[centerSpecular + 2U],
              0.0F, 1.0e-4F);

  (void)scene.QueueSet(scene.SceneRootEntity(), Engine::Ecs::Skybox{.Mode = Engine::Ecs::SkyboxMode::SolidColor,
                                                                    .EnvironmentGuid = {},
                                                                    .TextureGuid = {},
                                                                    .Color = {0.8F, 0.05F, 0.02F}});
  ASSERT_TRUE(scene.Update(0.0F));
  const auto solidColorOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(solidColorOutput.has_value()) << (solidColorOutput.has_value() ? "" : solidColorOutput.error().Message);
  const auto solidPixels = solidColorOutput->GetPixels(Engine::RenderAov::FinalColor);
  ASSERT_GT(solidPixels.size(), backgroundPixel + 3U);
  EXPECT_GT(solidPixels[backgroundPixel], 0.5F);
  EXPECT_LT(solidPixels[backgroundPixel + 1U], 0.1F);
  EXPECT_LT(solidPixels[backgroundPixel + 2U], 0.1F);
  const auto solidSpecular = solidColorOutput->GetPixels(Engine::RenderAov::SpecularLighting);
  ASSERT_GT(solidSpecular.size(), centerSpecular + 2U);
  EXPECT_NEAR(solidSpecular[centerSpecular] + solidSpecular[centerSpecular + 1U] + solidSpecular[centerSpecular + 2U],
              0.0F, 1.0e-4F);

  WriteSolidPng(project / "sky.png");
  const auto textureGuid = assets.ImportTexture("sky.png");
  ASSERT_TRUE(textureGuid.has_value()) << (textureGuid.has_value() ? "" : textureGuid.error().Message);
  ASSERT_TRUE(renderer.PrepareAssetTexture(*textureGuid, true, error)) << error;
  (void)scene.QueueSet(scene.SceneRootEntity(), Engine::Ecs::Skybox{.Mode = Engine::Ecs::SkyboxMode::LdrPanorama,
                                                                    .EnvironmentGuid = {},
                                                                    .TextureGuid = *textureGuid});
  ASSERT_TRUE(scene.Update(0.0F));
  const auto pngOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(pngOutput.has_value()) << (pngOutput.has_value() ? "" : pngOutput.error().Message);
  const auto pngPixels = pngOutput->GetPixels(Engine::RenderAov::FinalColor);
  ASSERT_GT(pngPixels.size(), backgroundPixel + 3U);
  EXPECT_GT(pngPixels[backgroundPixel], pngPixels[backgroundPixel + 1U] * 4.0F);
  EXPECT_LT(pngPixels[backgroundPixel + 2U], pngPixels[backgroundPixel]);
  const auto pngSpecular = pngOutput->GetPixels(Engine::RenderAov::SpecularLighting);
  ASSERT_GT(pngSpecular.size(), centerSpecular + 2U);
  EXPECT_NEAR(pngSpecular[centerSpecular] + pngSpecular[centerSpecular + 1U] + pngSpecular[centerSpecular + 2U], 0.0F,
              1.0e-4F);

  (void)scene.QueueSet(scene.SceneRootEntity(),
                       Engine::Ecs::Skybox{.EnvironmentGuid = *environmentGuid, .Intensity = 0.0F});
  ASSERT_TRUE(scene.Update(0.0F));

  auto target = renderer.CreateViewportTarget(imageSize, imageSize);
  ASSERT_NE(target, nullptr);
  Engine::Viewport viewport(target);
  Engine::ViewportBinding binding;
  binding.TargetScene = &scene;
  binding.TargetViewport = &viewport;
  binding.Camera = camera;
  for (const auto mode : {Engine::ViewportBinding::Mode::Preview, Engine::ViewportBinding::Mode::Editor}) {
    binding.RenderMode = mode;
    renderer.BeginFrame();
    renderer.SubmitViews({binding});
    renderer.EndFrame();
    const auto displayPixel = ReadViewportPixel(device.GetDevice(), viewport, 2, 2);
    EXPECT_GT(displayPixel[0], 30U) << "mode " << static_cast<int>(mode);
  }
  binding.RenderMode = Engine::ViewportBinding::Mode::Editor;
  binding.ShowEnvironmentBackground = false;
  renderer.BeginFrame();
  renderer.SubmitViews({binding});
  renderer.EndFrame();
  const auto hiddenPixel = ReadViewportPixel(device.GetDevice(), viewport, 2, 2);
  EXPECT_LT(hiddenPixel[0], 40U);

  std::error_code cleanupError;
  std::filesystem::remove_all(project, cleanupError);
}

TEST(RenderFinalTests, WARPFinalOutputUsesRequestedDimensionsAndOmitsDebugPass) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }

  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  Engine::Scene scene;
  Engine::Renderer::CameraView camera;
  constexpr uint32_t width = 13;
  constexpr uint32_t height = 7;
  auto result = renderer.RenderFinal(scene, camera, width, height);
  ASSERT_TRUE(result.has_value()) << (result.has_value() ? "" : result.error().Message);

  const auto& finalViews = renderer.GetRenderGraph()->GetBlackboard().GetSceneViews();
  ASSERT_EQ(finalViews.size(), 1U);
  EXPECT_EQ(finalViews.front()->RenderMode, Engine::ViewportBinding::Mode::Final);
  EXPECT_EQ(finalViews.front()->ViewportWidth, width);
  EXPECT_EQ(finalViews.front()->ViewportHeight, height);

  const Engine::RenderOutput& output = result.value();
  EXPECT_EQ(output.Width, width);
  EXPECT_EQ(output.Height, height);
  const auto planes = output.GetPlanes();
  ASSERT_EQ(planes.size(), 5U);
  for (const Engine::RenderOutputPlane& plane : planes) {
    EXPECT_EQ(plane.Pixels.size(), static_cast<size_t>(width) * height * plane.ChannelCount);
    EXPECT_EQ(plane.ChannelCount, Engine::RenderOutput::GetDescriptor(plane.Aov).ChannelCount);
  }

  const std::string finalGraph = renderer.GetRenderGraph()->Dump();
  EXPECT_NE(finalGraph.find("ForwardPass"), std::string::npos);
  EXPECT_EQ(finalGraph.find("DebugPass"), std::string::npos);

  auto editorTarget = renderer.CreateViewportTarget(width, height);
  ASSERT_NE(editorTarget, nullptr);
  Engine::Viewport viewport(editorTarget);
  Engine::ViewportBinding editorBinding;
  editorBinding.TargetScene = &scene;
  editorBinding.TargetViewport = &viewport;
  editorBinding.Camera = camera;

  renderer.BeginFrame();
  ASSERT_TRUE(renderer.IsFrameActive());
  renderer.SubmitViews({editorBinding});
  renderer.EndFrame();
  EXPECT_NE(renderer.GetRenderGraph()->Dump().find("DebugPass"), std::string::npos);
}

TEST(RenderFinalTests, WARPSceneViewportAndPipMatchPngDisplayTransformWhileFinalAovStaysLinear) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }
  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  Engine::Renderer::MaterialProperties opaqueProperties;
  opaqueProperties.BaseColor = {0.0F, 0.0F, 0.0F, 1.0F};
  opaqueProperties.Emissive = {0.5F, 0.0F, 0.0F};
  const auto opaqueMaterial = renderer.CreateMaterial(opaqueProperties);
  ASSERT_NE(opaqueMaterial, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Cube, opaqueMaterial));
  Engine::Renderer::MaterialProperties transparentProperties;
  transparentProperties.BaseColor = {0.0F, 0.0F, 0.0F, 0.5F};
  transparentProperties.Emissive = {0.0F, 0.5F, 0.0F};
  transparentProperties.AlphaMode = Engine::Renderer::MaterialAlphaMode::Blend;
  const auto transparentMaterial = renderer.CreateMaterial(transparentProperties);
  ASSERT_NE(transparentMaterial, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Sphere, transparentMaterial));

  Engine::Scene scene;
  const uint64_t opaqueSequence = scene.QueueCreate("Opaque display target");
  const uint64_t transparentSequence = scene.QueueCreate("Transparent display target");
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 2U);
  ASSERT_EQ(created.at(0).Sequence, opaqueSequence);
  ASSERT_EQ(created.at(1).Sequence, transparentSequence);
  ASSERT_TRUE(created.front().Outcome);
  ASSERT_TRUE(created.at(1).Outcome);
  (void)scene.QueueAdd(created.at(0).Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "cube"});
  (void)scene.QueueAdd(created.at(1).Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "sphere"});
  Engine::Ecs::LocalTransform opaqueTransform;
  opaqueTransform.Scale = {2.0F, 2.0F, 2.0F};
  (void)scene.QueueSet(created.at(0).Entity, opaqueTransform);
  Engine::Ecs::LocalTransform transparentTransform;
  transparentTransform.Position = {0.0F, 0.0F, -1.0F};
  transparentTransform.Scale = {1.0F, 1.0F, 1.0F};
  (void)scene.QueueSet(created.at(1).Entity, transparentTransform);
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -5.0F};
  constexpr uint32_t imageSize = 32;
  const auto finalOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(finalOutput.has_value()) << (finalOutput.has_value() ? "" : finalOutput.error().Message);
  const auto finalPixels = finalOutput->GetPixels(Engine::RenderAov::FinalColor);
  const size_t centerOffset = (static_cast<size_t>(imageSize / 2U) * imageSize + imageSize / 2U) * 4U;
  ASSERT_GT(finalPixels.size(), centerOffset + 2U);
  const std::array<float, 3> linearColor = {finalPixels[centerOffset], finalPixels[centerOffset + 1U],
                                            finalPixels[centerOffset + 2U]};
  EXPECT_NEAR(linearColor[0], 0.25F, 0.02F);
  EXPECT_NEAR(linearColor[1], 0.25F, 0.02F);
  EXPECT_NEAR(linearColor[2], 0.0F, 0.02F);

  const Engine::Ecs::CameraPostProcessing grading{
      .Effects = {Engine::Ecs::ColorGradingEffect{.Exposure = 1.0F, .Contrast = 1.0F, .Saturation = 1.0F}}};
  const auto gradedOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, grading);
  ASSERT_TRUE(gradedOutput.has_value()) << (gradedOutput.has_value() ? "" : gradedOutput.error().Message);
  const auto gradedPixels = gradedOutput->GetPixels(Engine::RenderAov::FinalColor);
  EXPECT_NEAR(gradedPixels[centerOffset], linearColor[0] * 2.0F, 0.02F);
  EXPECT_NEAR(gradedPixels[centerOffset + 1U], linearColor[1] * 2.0F, 0.02F);
  EXPECT_TRUE(std::ranges::equal(finalOutput->GetPixels(Engine::RenderAov::DiffuseLighting),
                                 gradedOutput->GetPixels(Engine::RenderAov::DiffuseLighting)));

  const Engine::Ecs::CameraPostProcessing bloom{
      .Effects = {Engine::Ecs::BloomEffect{.Threshold = 0.01F, .Intensity = 1.0F, .Radius = 16.0F}}};
  const auto bloomedOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, bloom);
  ASSERT_TRUE(bloomedOutput.has_value()) << (bloomedOutput.has_value() ? "" : bloomedOutput.error().Message);
  const auto bloomedPixels = bloomedOutput->GetPixels(Engine::RenderAov::FinalColor);
  const size_t haloOffset = (static_cast<size_t>(imageSize / 2U) * imageSize) * 4U;
  EXPECT_GT(bloomedPixels[haloOffset], finalPixels[haloOffset]);
  EXPECT_GT(bloomedPixels[haloOffset + 3U], finalPixels[haloOffset + 3U]);

  auto target = renderer.CreateViewportTarget(imageSize, imageSize);
  ASSERT_NE(target, nullptr);
  Engine::Viewport viewport(target);
  Engine::ViewportBinding binding;
  binding.TargetScene = &scene;
  binding.TargetViewport = &viewport;
  binding.Camera = camera;
  for (const Engine::ViewportBinding::Mode mode :
       {Engine::ViewportBinding::Mode::Preview, Engine::ViewportBinding::Mode::Editor}) {
    binding.RenderMode = mode;
    renderer.BeginFrame();
    renderer.SubmitViews({binding});
    renderer.EndFrame();

    const auto displayPixel = ReadViewportPixel(device.GetDevice(), viewport, imageSize / 2U, imageSize / 2U);
    for (size_t channel = 0; channel < linearColor.size(); ++channel) {
      const uint8_t expected = static_cast<uint8_t>(std::lround(LinearToDisplay(linearColor[channel]) * 255.0F));
      EXPECT_NEAR(displayPixel[channel], expected, 2U)
          << "mode " << static_cast<int>(mode) << " RGB channel " << channel;
      if (linearColor[channel] > 0.0F) {
        EXPECT_GT(displayPixel[channel], static_cast<uint8_t>(std::lround(linearColor[channel] * 255.0F)))
            << "display output should encode linear AOV values for channel " << channel;
      }
    }
  }

  auto gradedTarget = renderer.CreateViewportTarget(imageSize, imageSize);
  ASSERT_NE(gradedTarget, nullptr);
  Engine::Viewport gradedViewport(gradedTarget);
  Engine::ViewportBinding gradedBinding = binding;
  gradedBinding.TargetViewport = &gradedViewport;
  gradedBinding.RenderMode = Engine::ViewportBinding::Mode::Preview;
  gradedBinding.PostProcessing = grading;
  gradedBinding.PostProcessing.Effects.emplace_back(Engine::Ecs::FxaaEffect{});
  binding.RenderMode = Engine::ViewportBinding::Mode::Preview;
  renderer.BeginFrame();
  renderer.SubmitViews({binding, gradedBinding});
  renderer.EndFrame();

  const auto baselinePixel = ReadViewportPixel(device.GetDevice(), viewport, imageSize / 2U, imageSize / 2U);
  const auto gradedPixel = ReadViewportPixel(device.GetDevice(), gradedViewport, imageSize / 2U, imageSize / 2U);
  for (size_t channel = 0; channel < linearColor.size(); ++channel) {
    const uint8_t baselineExpected = static_cast<uint8_t>(std::lround(LinearToDisplay(linearColor[channel]) * 255.0F));
    const uint8_t gradedExpected =
        static_cast<uint8_t>(std::lround(LinearToDisplay(linearColor[channel] * 2.0F) * 255.0F));
    EXPECT_NEAR(baselinePixel[channel], baselineExpected, 2U) << "baseline view channel " << channel;
    EXPECT_NEAR(gradedPixel[channel], gradedExpected, 2U) << "graded view channel " << channel;
  }
}

TEST(RenderFinalTests, WARPFinalColorReadbackUsesStraightAlphaAndPreservesBloomHaloEnergy) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }
  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  Engine::Renderer::MaterialProperties properties;
  properties.BaseColor = {0.0F, 0.0F, 0.0F, 0.5F};
  properties.Emissive = {0.5F, 0.5F, 0.5F};
  properties.AlphaMode = Engine::Renderer::MaterialAlphaMode::Blend;
  const auto material = renderer.CreateMaterial(properties);
  ASSERT_NE(material, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Sphere, material));

  Engine::Scene scene;
  const uint64_t sequence = scene.QueueCreate("Straight-alpha bloom target");
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 1U);
  ASSERT_EQ(created.front().Sequence, sequence);
  ASSERT_TRUE(created.front().Outcome);
  (void)scene.QueueAdd(created.front().Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "sphere"});
  Engine::Ecs::LocalTransform transform;
  transform.Scale = {1.5F, 1.5F, 1.5F};
  (void)scene.QueueSet(created.front().Entity, transform);
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -5.0F};
  constexpr uint32_t imageSize = 64;
  const auto baseline = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(baseline.has_value()) << (baseline.has_value() ? "" : baseline.error().Message);
  const auto baselinePixels = baseline->GetPixels(Engine::RenderAov::FinalColor);
  const size_t centerOffset = (static_cast<size_t>(imageSize / 2U) * imageSize + imageSize / 2U) * 4U;
  EXPECT_NEAR(baselinePixels[centerOffset], 0.5F, 0.02F);
  EXPECT_NEAR(baselinePixels[centerOffset + 1U], 0.5F, 0.02F);
  EXPECT_NEAR(baselinePixels[centerOffset + 2U], 0.5F, 0.02F);
  EXPECT_NEAR(baselinePixels[centerOffset + 3U], 0.5F, 0.02F);

  const Engine::Ecs::CameraPostProcessing bloom{
      .Effects = {Engine::Ecs::BloomEffect{.Threshold = 0.01F, .Intensity = 1.0F, .Radius = 16.0F}}};
  const auto bloomed = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, bloom);
  ASSERT_TRUE(bloomed.has_value()) << (bloomed.has_value() ? "" : bloomed.error().Message);
  const auto bloomedPixels = bloomed->GetPixels(Engine::RenderAov::FinalColor);
  const size_t haloOffset = (static_cast<size_t>(12U) * imageSize + imageSize / 2U) * 4U;
  EXPECT_NEAR(baselinePixels[haloOffset + 3U], 0.0F, 1.0e-5F);
  EXPECT_GT(bloomedPixels[haloOffset + 3U], 1.0e-5F);
  EXPECT_LT(bloomedPixels[haloOffset + 3U], 1.0F);
  EXPECT_NEAR(bloomedPixels[haloOffset], 1.0F, 0.05F);
  EXPECT_NEAR(bloomedPixels[haloOffset + 1U], 1.0F, 0.05F);
  EXPECT_NEAR(bloomedPixels[haloOffset + 2U], 1.0F, 0.05F);
}

TEST(RenderFinalTests, WARPCameraFxaaSmoothsFinalColorEdgesAndPreservesOtherAovs) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }
  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  Engine::Renderer::MaterialProperties properties;
  properties.BaseColor = {0.0F, 0.0F, 0.0F, 1.0F};
  properties.Emissive = {1.0F, 0.5F, 0.25F};
  const auto material = renderer.CreateMaterial(properties);
  ASSERT_NE(material, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Cube, material));

  Engine::Scene scene;
  (void)scene.QueueCreate("FXAA edge target");
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 1U);
  ASSERT_TRUE(created.front().Outcome);
  (void)scene.QueueAdd(created.front().Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "cube"});
  Engine::Ecs::LocalTransform transform;
  transform.Scale = {2.0F, 2.0F, 2.0F};
  (void)scene.QueueSet(created.front().Entity, transform);
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -5.0F};
  constexpr uint32_t imageSize = 64;
  const auto baseline = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(baseline.has_value()) << (baseline.has_value() ? "" : baseline.error().Message);
  const Engine::Ecs::CameraPostProcessing fxaa{.Effects = {Engine::Ecs::FxaaEffect{}}};
  const auto antialiased = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, fxaa);
  ASSERT_TRUE(antialiased.has_value()) << (antialiased.has_value() ? "" : antialiased.error().Message);

  const auto baselineColor = baseline->GetPixels(Engine::RenderAov::FinalColor);
  const auto antialiasedColor = antialiased->GetPixels(Engine::RenderAov::FinalColor);
  ASSERT_EQ(baselineColor.size(), antialiasedColor.size());
  float colorDifference = 0.0F;
  for (size_t index = 0; index < baselineColor.size(); ++index) {
    colorDifference += std::abs(baselineColor[index] - antialiasedColor[index]);
    if (index % 4U == 3U) {
      EXPECT_FLOAT_EQ(baselineColor[index], antialiasedColor[index]);
    }
  }
  EXPECT_GT(colorDifference, 0.01F);
  EXPECT_TRUE(std::ranges::equal(antialiased->GetPixels(Engine::RenderAov::Depth),
                                 baseline->GetPixels(Engine::RenderAov::Depth)));
  EXPECT_TRUE(std::ranges::equal(antialiased->GetPixels(Engine::RenderAov::WorldNormal),
                                 baseline->GetPixels(Engine::RenderAov::WorldNormal)));
  EXPECT_TRUE(std::ranges::equal(antialiased->GetPixels(Engine::RenderAov::SpecularLighting),
                                 baseline->GetPixels(Engine::RenderAov::SpecularLighting)));
  EXPECT_TRUE(std::ranges::equal(antialiased->GetPixels(Engine::RenderAov::DiffuseLighting),
                                 baseline->GetPixels(Engine::RenderAov::DiffuseLighting)));

  const auto totalDifference = [](std::span<const float> left, std::span<const float> right) -> float {
    float difference = 0.0F;
    for (size_t index = 0; index < left.size(); ++index) {
      difference += std::abs(left[index] - right[index]);
    }
    return difference;
  };
  const Engine::Ecs::CameraPostProcessing fxaaLow{
      .Effects = {Engine::Ecs::FxaaEffect{.Quality = Engine::Ecs::FxaaQuality::Low}}};
  const Engine::Ecs::CameraPostProcessing fxaaHigh{
      .Effects = {Engine::Ecs::FxaaEffect{.Quality = Engine::Ecs::FxaaQuality::High}}};
  const Engine::Ecs::CameraPostProcessing fxaaRaisedThreshold{
      .Effects = {Engine::Ecs::FxaaEffect{.Quality = Engine::Ecs::FxaaQuality::Medium, .EdgeThreshold = 0.5F}}};
  const auto lowQuality = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, fxaaLow);
  const auto highQuality = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, fxaaHigh);
  const auto raisedThreshold = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, fxaaRaisedThreshold);
  ASSERT_TRUE(lowQuality.has_value());
  ASSERT_TRUE(highQuality.has_value());
  ASSERT_TRUE(raisedThreshold.has_value());
  EXPECT_GT(totalDifference(lowQuality->GetPixels(Engine::RenderAov::FinalColor),
                            highQuality->GetPixels(Engine::RenderAov::FinalColor)),
            0.001F);
  EXPECT_TRUE(std::ranges::all_of(raisedThreshold->GetPixels(Engine::RenderAov::FinalColor),
                                  [](float value) { return std::isfinite(value); }));

  const Engine::Ecs::BloomEffect bloom{.Threshold = 0.2F, .Intensity = 0.4F, .Radius = 4.0F};
  const Engine::Ecs::CameraPostProcessing bloomThenFxaa{.Effects = {bloom, Engine::Ecs::FxaaEffect{}}};
  const Engine::Ecs::CameraPostProcessing fxaaThenBloom{.Effects = {Engine::Ecs::FxaaEffect{}, bloom}};
  const auto bloomedAfterFxaa = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, fxaaThenBloom);
  const auto fxaaAfterBloom = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, bloomThenFxaa);
  ASSERT_TRUE(bloomedAfterFxaa.has_value());
  ASSERT_TRUE(fxaaAfterBloom.has_value());
  EXPECT_GT(totalDifference(bloomedAfterFxaa->GetPixels(Engine::RenderAov::FinalColor),
                            fxaaAfterBloom->GetPixels(Engine::RenderAov::FinalColor)),
            0.01F);
  const Engine::Ecs::BloomEffect smoothBloom{.Threshold = 0.2F, .Intensity = 0.4F, .Radius = 4.0F, .Smooth = true};
  const Engine::Ecs::BloomEffect sharpBloom{.Threshold = 0.2F, .Intensity = 0.4F, .Radius = 4.0F, .Smooth = false};
  const auto smoothBloomOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {},
                                                      Engine::Ecs::CameraPostProcessing{.Effects = {smoothBloom}});
  const auto sharpBloomOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {},
                                                     Engine::Ecs::CameraPostProcessing{.Effects = {sharpBloom}});
  ASSERT_TRUE(smoothBloomOutput.has_value());
  ASSERT_TRUE(sharpBloomOutput.has_value());
  EXPECT_GT(totalDifference(smoothBloomOutput->GetPixels(Engine::RenderAov::FinalColor),
                            sharpBloomOutput->GetPixels(Engine::RenderAov::FinalColor)),
            0.001F);
}

TEST(RenderFinalTests, WARPSubmitsIndependentEditorAndPreviewViewsAndSkipsInvalidTargets) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }

  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  Engine::Scene scene;
  auto editorTarget = renderer.CreateViewportTarget(20, 10);
  auto previewTarget = renderer.CreateViewportTarget(6, 3);
  ASSERT_NE(editorTarget, nullptr);
  ASSERT_NE(previewTarget, nullptr);
  Engine::Viewport editorViewport(editorTarget);
  Engine::Viewport previewViewport(previewTarget);

  Engine::ViewportBinding editorBinding;
  editorBinding.TargetScene = &scene;
  editorBinding.TargetViewport = &editorViewport;
  editorBinding.Camera.Position = {1.0F, 2.0F, 3.0F};
  editorBinding.DebugSettings.ShowWireframePlane = true;

  Engine::ViewportBinding previewBinding;
  previewBinding.TargetScene = &scene;
  previewBinding.TargetViewport = &previewViewport;
  previewBinding.Camera.Position = {-4.0F, 5.0F, -6.0F};
  previewBinding.RenderMode = Engine::ViewportBinding::Mode::Preview;
  previewBinding.PostProcessing.AmbientOcclusion = Engine::Ecs::ScreenSpaceAmbientOcclusionSettings{};
  previewBinding.DebugSettings.ShowWireframePlane = true;

  Engine::ViewportBinding releasedBinding = previewBinding;
  Engine::Viewport releasedViewport(4, 4, nullptr);
  releasedBinding.TargetViewport = &releasedViewport;

  Engine::ViewportBinding zeroSizeBinding = editorBinding;
  Engine::Viewport zeroSizeViewport(0, 4, nullptr);
  zeroSizeBinding.TargetViewport = &zeroSizeViewport;

  Engine::ViewportBinding mismatchedFinalBinding = editorBinding;
  mismatchedFinalBinding.RenderMode = Engine::ViewportBinding::Mode::Final;
  mismatchedFinalBinding.OutputWidth = 19;
  mismatchedFinalBinding.OutputHeight = 10;

  renderer.BeginFrame();
  renderer.SubmitViews({editorBinding, previewBinding, releasedBinding, zeroSizeBinding, mismatchedFinalBinding});
  EXPECT_EQ(renderer.GetRenderGraph()->GetBlackboard().GetSceneViews().size(), 2U);
  renderer.EndFrame();

  const auto& views = renderer.GetRenderGraph()->GetBlackboard().GetSceneViews();
  ASSERT_EQ(views.size(), 2U);
  const Engine::Renderer::RenderView* editorView = views.at(0);
  const Engine::Renderer::RenderView* previewView = views.at(1);
  EXPECT_EQ(editorView->RenderMode, Engine::ViewportBinding::Mode::Editor);
  EXPECT_EQ(previewView->RenderMode, Engine::ViewportBinding::Mode::Preview);
  EXPECT_EQ(editorView->ViewportWidth, 20U);
  EXPECT_EQ(editorView->ViewportHeight, 10U);
  EXPECT_EQ(previewView->ViewportWidth, 6U);
  EXPECT_EQ(previewView->ViewportHeight, 3U);
  EXPECT_NE(editorView->TargetFramebuffer, previewView->TargetFramebuffer);
  EXPECT_NE(editorView->TargetFramebuffer->getDesc().colorAttachments.at(0).texture,
            previewView->TargetFramebuffer->getDesc().colorAttachments.at(0).texture);
  EXPECT_FALSE(editorView->AmbientOcclusionTexture.isValid());
  EXPECT_TRUE(previewView->AmbientOcclusionTexture.isValid());
  EXPECT_TRUE(previewView->AmbientOcclusionDepth.isValid());
  EXPECT_TRUE(previewView->AmbientOcclusionNormal.isValid());
  EXPECT_FLOAT_EQ(editorView->CameraPosition.x, 1.0F);
  EXPECT_FLOAT_EQ(editorView->CameraPosition.y, 2.0F);
  EXPECT_FLOAT_EQ(editorView->CameraPosition.z, 3.0F);
  EXPECT_FLOAT_EQ(previewView->CameraPosition.x, -4.0F);
  EXPECT_FLOAT_EQ(previewView->CameraPosition.y, 5.0F);
  EXPECT_FLOAT_EQ(previewView->CameraPosition.z, -6.0F);
  EXPECT_FALSE(editorView->DebugGeometryData.Empty());
  EXPECT_TRUE(previewView->DebugGeometryData.Empty());
  EXPECT_NE(renderer.GetRenderGraph()->Dump().find("DebugPass"), std::string::npos);

  renderer.BeginFrame();
  renderer.SubmitViews({previewBinding});
  renderer.EndFrame();
  EXPECT_EQ(renderer.GetRenderGraph()->GetBlackboard().GetSceneViews().size(), 1U);
  EXPECT_EQ(renderer.GetRenderGraph()->GetBlackboard().GetSceneViews().front()->RenderMode,
            Engine::ViewportBinding::Mode::Preview);
  EXPECT_EQ(renderer.GetRenderGraph()->Dump().find("DebugPass"), std::string::npos);
}

TEST(RenderFinalTests, WARPTransparentLightingAovsCompositeLikeFinalColor) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }

  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  Engine::Renderer::MaterialProperties backgroundProperties;
  backgroundProperties.BaseColor = {0.0F, 0.0F, 0.0F, 1.0F};
  backgroundProperties.Emissive = {0.0F, 0.0F, 1.0F};
  const auto backgroundMaterial = renderer.CreateMaterial(backgroundProperties);
  ASSERT_NE(backgroundMaterial, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Cube, backgroundMaterial));

  Engine::Renderer::MaterialProperties transparentProperties;
  transparentProperties.BaseColor = {0.0F, 0.0F, 0.0F, 0.5F};
  transparentProperties.Emissive = {1.0F, 0.0F, 0.0F};
  transparentProperties.AlphaMode = Engine::Renderer::MaterialAlphaMode::Blend;
  const auto transparentMaterial = renderer.CreateMaterial(transparentProperties);
  ASSERT_NE(transparentMaterial, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Sphere, transparentMaterial));

  Engine::Scene scene;
  const uint64_t backgroundCreate = scene.QueueCreate("AOV background");
  const uint64_t transparentCreate = scene.QueueCreate("AOV transparent foreground");
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 2U);
  ASSERT_TRUE(created.at(0).Outcome);
  ASSERT_TRUE(created.at(1).Outcome);
  ASSERT_EQ(created.at(0).Sequence, backgroundCreate);
  ASSERT_EQ(created.at(1).Sequence, transparentCreate);

  Engine::Ecs::LocalTransform backgroundTransform;
  backgroundTransform.Position = {0.0F, 0.0F, 1.0F};
  backgroundTransform.Scale = {3.0F, 3.0F, 3.0F};
  (void)scene.QueueSet(created.at(0).Entity, backgroundTransform);
  (void)scene.QueueAdd(created.at(0).Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "cube"});

  Engine::Ecs::LocalTransform transparentTransform;
  transparentTransform.Position = {0.0F, 0.0F, -1.0F};
  (void)scene.QueueSet(created.at(1).Entity, transparentTransform);
  (void)scene.QueueAdd(created.at(1).Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "sphere"});
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -5.0F};
  constexpr uint32_t width = 16;
  constexpr uint32_t height = 16;
  const auto output = renderer.RenderFinal(scene, camera, width, height);
  ASSERT_TRUE(output.has_value()) << (output.has_value() ? "" : output.error().Message);

  const auto finalColor = output->GetPixels(Engine::RenderAov::FinalColor);
  const auto specular = output->GetPixels(Engine::RenderAov::SpecularLighting);
  const auto diffuse = output->GetPixels(Engine::RenderAov::DiffuseLighting);
  const size_t center = (static_cast<size_t>(height / 2U) * width + width / 2U);
  ASSERT_GT(finalColor.size(), center * 4U + 3U);
  ASSERT_GT(specular.size(), center * 3U + 2U);
  ASSERT_GT(diffuse.size(), center * 3U + 2U);
  for (size_t channel = 0; channel < 3U; ++channel) {
    EXPECT_NEAR(finalColor[center * 4U + channel], specular[center * 3U + channel] + diffuse[center * 3U + channel],
                1.0e-4F);
  }
  EXPECT_NEAR(finalColor[center * 4U], 0.5F, 1.0e-2F);
  EXPECT_NEAR(finalColor[center * 4U + 2U], 0.5F, 1.0e-2F);
}

TEST(RenderFinalTests, WARPAllEditorExposedPrimitivesRender) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }

  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  Engine::Renderer::MaterialProperties properties;
  properties.BaseColor = {0.0F, 0.0F, 0.0F, 1.0F};
  properties.Emissive = {1.0F, 0.0F, 0.0F};
  const auto material = renderer.CreateMaterial(properties);
  ASSERT_NE(material, nullptr);

  constexpr std::array shapes = {Engine::PrimitiveShape::Cube, Engine::PrimitiveShape::Sphere,
                                 Engine::PrimitiveShape::Plane, Engine::PrimitiveShape::Cone};
  constexpr std::array names = {"cube", "sphere", "plane", "cone"};
  constexpr std::array flipStates = {std::array<bool, 2>{false, false}, std::array<bool, 2>{true, false},
                                     std::array<bool, 2>{false, true}, std::array<bool, 2>{true, true}};
  for (const auto shape : shapes) {
    ASSERT_TRUE(renderer.CreatePrimitive(shape, material));
  }

  for (size_t index = 0; index < shapes.size(); ++index) {
    std::array<float, 3> unflippedCenterNormal{};
    bool hasUnflippedCenterNormal = false;
    for (const auto& [flipHorizontal, flipVertical] : flipStates) {
      Engine::Scene scene;
      const uint64_t sequence = scene.QueueCreate(names[index]);
      const auto created = scene.sync();
      ASSERT_EQ(created.size(), 1U);
      ASSERT_EQ(created.front().Sequence, sequence);
      ASSERT_TRUE(created.front().Outcome);

      Engine::Ecs::LocalTransform transform;
      transform.Position = {0.0F, 0.0F, 0.0F};
      transform.FlipHorizontal = flipHorizontal;
      transform.FlipVertical = flipVertical;
      if (shapes[index] == Engine::PrimitiveShape::Plane) {
        DirectX::XMFLOAT4 rotation{};
        DirectX::XMStoreFloat4(&rotation, DirectX::XMQuaternionRotationRollPitchYaw(-DirectX::XM_PIDIV2, 0.0F, 0.0F));
        transform.Rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
      }
      (void)scene.QueueSet(created.front().Entity, transform);
      (void)scene.QueueAdd(created.front().Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = names[index]});
      ASSERT_TRUE(scene.Update(0.0F));

      Engine::Renderer::CameraView camera;
      camera.Position = {0.0F, 0.0F, -5.0F};
      const auto output = renderer.RenderFinal(scene, camera, 32, 32);
      ASSERT_TRUE(output.has_value()) << (output.has_value() ? "" : output.error().Message);
      const auto pixels = output->GetPixels(Engine::RenderAov::FinalColor);
      size_t illuminatedPixelCount = 0;
      for (size_t pixel = 0; pixel + 3 < pixels.size(); pixel += 4) {
        if (pixels[pixel] > 0.1F) {
          ++illuminatedPixelCount;
        }
      }
      EXPECT_GT(illuminatedPixelCount, 4U)
          << names[index] << " flipped X=" << flipHorizontal << " flipped Y=" << flipVertical;
      if (shapes[index] == Engine::PrimitiveShape::Cube) {
        const auto normals = output->GetPixels(Engine::RenderAov::WorldNormal);
        constexpr size_t imageSide = 32U;
        constexpr size_t centerNormalOffset = ((imageSide / 2U) * imageSide + (imageSide / 2U)) * 3U;
        ASSERT_GT(normals.size(), centerNormalOffset + 2U);
        if (!hasUnflippedCenterNormal) {
          std::copy_n(normals.begin() + static_cast<std::ptrdiff_t>(centerNormalOffset), unflippedCenterNormal.size(),
                      unflippedCenterNormal.begin());
          hasUnflippedCenterNormal = true;
        } else {
          for (size_t channel = 0; channel < unflippedCenterNormal.size(); ++channel) {
            EXPECT_NEAR(normals[centerNormalOffset + channel], unflippedCenterNormal[channel], 1.0e-2F)
                << "flipped X=" << flipHorizontal << " flipped Y=" << flipVertical;
          }
        }
      }
    }
  }
}

TEST(RenderFinalTests, WARPAssignedMaterialTextureGuidsReachLiveTextureResolver) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }

  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  constexpr char baseColorGuid[] = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
  constexpr char normalGuid[] = "11111111-2222-4333-8444-555555555555";
  const std::vector<Engine::AssetManager::TextureAssetInfo> textures = {{.guid = baseColorGuid, .name = "Base color"},
                                                                        {.guid = normalGuid, .name = "Normal"}};
  Engine::SceneMaterialDocument material;
  std::string error;
  for (const auto& [guid, slot] : std::array<std::pair<const char*, MaterialTextureSlot>, 2>{
           {{baseColorGuid, MaterialTextureSlot::BaseColor}, {normalGuid, MaterialTextureSlot::Normal}}}) {
    const auto payload = MakeAssetDragDropPayload(guid);
    ASSERT_TRUE(payload);
    const auto decoded = ReadAssetDragDropGuid(&*payload, static_cast<int>(sizeof(*payload)));
    ASSERT_TRUE(decoded);
    ASSERT_TRUE(AssignDroppedTexture(material, slot, *decoded, textures, error)) << error;
  }
  ASSERT_EQ(material.baseColorTextureGuid, std::optional<std::string>{baseColorGuid});
  ASSERT_EQ(material.normalTextureGuid, std::optional<std::string>{normalGuid});

  Engine::Renderer::MaterialProperties properties;
  const auto defaultMaterial = renderer.CreateMaterial(properties);
  ASSERT_NE(defaultMaterial, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Cube, defaultMaterial));

  std::array<std::shared_ptr<Engine::Renderer::TextureResource>, 2> gpuTextures;
  Engine::ImportedImage baseColorImage{.name = "red base color",
                                       .mimeType = "image/raw",
                                       .width = 1,
                                       .height = 1,
                                       .rgba8 = {255, 0, 0, 255}};
  gpuTextures[0] = renderer.GetOrCreateTexture(baseColorGuid, 1U, baseColorImage, true, error);
  ASSERT_NE(gpuTextures[0], nullptr) << error;
  Engine::ImportedImage normalImage{.name = "flat normal",
                                    .mimeType = "image/raw",
                                    .width = 1,
                                    .height = 1,
                                    .rgba8 = {128, 128, 255, 255}};
  gpuTextures[1] = renderer.GetOrCreateTexture(normalGuid, 1U, normalImage, false, error, nullptr, true);
  ASSERT_NE(gpuTextures[1], nullptr) << error;

  std::vector<std::tuple<std::string, bool, bool, bool, float>> resolved;
  renderer.GetSceneRenderer()->SetAssetTextureResolver(
      [&resolved, &gpuTextures](const std::string& guid, bool srgb, bool normalMap, bool preserveAlphaCoverage,
                                float alphaCutoff) -> std::shared_ptr<Engine::Renderer::TextureResource> {
        resolved.emplace_back(guid, srgb, normalMap, preserveAlphaCoverage, alphaCutoff);
        if (guid == "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee" && srgb) {
          return gpuTextures[0];
        }
        if (guid == "11111111-2222-4333-8444-555555555555" && !srgb) {
          return gpuTextures[1];
        }
        return nullptr;
      });

  Engine::Scene scene;
  const uint64_t sequence = scene.QueueCreate("Textured entity");
  const uint64_t lightSequence =
      scene.QueueCreate("Texture test light", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::SpotLight);
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 2U);
  ASSERT_EQ(created.front().Sequence, sequence);
  ASSERT_EQ(created.at(1).Sequence, lightSequence);
  ASSERT_TRUE(created.front().Outcome);
  ASSERT_TRUE(created.at(1).Outcome);
  const Engine::EntityHandle meshEntity = created.front().Entity;
  const Engine::EntityHandle lightEntity = created.at(1).Entity;
  Engine::SceneMaterialDocument materialWithoutTextures = material;
  materialWithoutTextures.baseColorTextureGuid.reset();
  materialWithoutTextures.normalTextureGuid.reset();
  Engine::SceneMaterialDocument materialBaseColorOnly = material;
  materialBaseColorOnly.normalTextureGuid.reset();
  (void)scene.QueueAdd(meshEntity,
                       Engine::Ecs::MeshRenderer{.PrimitiveType = "cube", .Material = materialWithoutTextures});
  Engine::Ecs::Light light;
  light.Type = Engine::Ecs::LightType::Point;
  light.Intensity = 20.0F;
  light.Range = 20.0F;
  light.CastShadows = false;
  (void)scene.QueueSet(lightEntity, light);
  Engine::Ecs::LocalTransform lightTransform;
  lightTransform.Position = {0.0F, 0.0F, -3.0F};
  (void)scene.QueueSet(lightEntity, lightTransform);
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -5.0F};
  const auto withoutTextures = renderer.RenderFinal(scene, camera, 32, 32);
  ASSERT_TRUE(withoutTextures.has_value()) << (withoutTextures.has_value() ? "" : withoutTextures.error().Message);

  (void)scene.QueueSet(meshEntity,
                       Engine::Ecs::MeshRenderer{.PrimitiveType = "cube", .Material = materialBaseColorOnly});
  ASSERT_TRUE(scene.Update(0.0F));
  const auto baseColorOutput = renderer.RenderFinal(scene, camera, 32, 32);
  ASSERT_TRUE(baseColorOutput.has_value()) << (baseColorOutput.has_value() ? "" : baseColorOutput.error().Message);
  ASSERT_EQ(resolved.size(), 1U);
  EXPECT_EQ(resolved[0], (std::tuple<std::string, bool, bool, bool, float>{baseColorGuid, true, false, false, 0.5F}));
  const auto baseColorWarmOutput = renderer.RenderFinal(scene, camera, 32, 32);
  ASSERT_TRUE(baseColorWarmOutput.has_value())
      << (baseColorWarmOutput.has_value() ? "" : baseColorWarmOutput.error().Message);

  resolved.clear();
  (void)scene.QueueSet(meshEntity, Engine::Ecs::MeshRenderer{.PrimitiveType = "cube", .Material = material});
  ASSERT_TRUE(scene.Update(0.0F));
  const auto output = renderer.RenderFinal(scene, camera, 32, 32);
  ASSERT_TRUE(output.has_value()) << (output.has_value() ? "" : output.error().Message);
  ASSERT_EQ(resolved.size(), 2U);
  EXPECT_EQ(resolved[0], (std::tuple<std::string, bool, bool, bool, float>{baseColorGuid, true, false, false, 0.5F}));
  EXPECT_EQ(resolved[1], (std::tuple<std::string, bool, bool, bool, float>{normalGuid, false, true, false, 0.5F}));
  ASSERT_TRUE(renderer.RenderFinal(scene, camera, 32, 32).has_value());

  const auto untextured = withoutTextures->GetPixels(Engine::RenderAov::FinalColor);
  const auto textured = baseColorWarmOutput->GetPixels(Engine::RenderAov::FinalColor);
  ASSERT_FALSE(untextured.empty());
  ASSERT_EQ(textured.size(), untextured.size());
  float largestDifference = 0.0F;
  for (size_t pixel = 0; pixel + 3U < textured.size(); pixel += 4U) {
    largestDifference = std::max({largestDifference, std::abs(textured[pixel] - untextured[pixel]),
                                  std::abs(textured[pixel + 1U] - untextured[pixel + 1U]),
                                  std::abs(textured[pixel + 2U] - untextured[pixel + 2U])});
  }
  EXPECT_GT(largestDifference, 0.1F) << "authored texture references should affect rendered output";
}

TEST(RenderFinalTests, WARPAllLightTypesChangeReceivingGeometryWhenShadowsAreEnabled) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }

  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  Engine::Renderer::MaterialProperties receiverProperties;
  receiverProperties.BaseColor = {0.8F, 0.8F, 0.8F, 1.0F};
  receiverProperties.Roughness = 1.0F;
  receiverProperties.Model = Engine::Renderer::BrdfModel::AshikhminShirley;
  receiverProperties.AnisotropicRoughnessU = 0.2F;
  receiverProperties.AnisotropicRoughnessV = 0.8F;
  const auto receiverMaterial = renderer.CreateMaterial(receiverProperties);
  ASSERT_NE(receiverMaterial, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Plane, receiverMaterial));

  Engine::Renderer::MaterialProperties casterProperties;
  casterProperties.BaseColor = {0.8F, 0.2F, 0.1F, 1.0F};
  casterProperties.Roughness = 1.0F;
  const auto casterMaterial = renderer.CreateMaterial(casterProperties);
  ASSERT_NE(casterMaterial, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Cube, casterMaterial));

  constexpr std::array lightTypes = {Engine::Ecs::LightType::Directional, Engine::Ecs::LightType::Point,
                                     Engine::Ecs::LightType::Spot};
  constexpr std::array lightNames = {"Directional", "Point", "Spot"};
  for (size_t lightIndex = 0; lightIndex < lightTypes.size(); ++lightIndex) {
    Engine::Scene scene;
    const uint64_t receiverSequence = scene.QueueCreate("Shadow receiver");
    const uint64_t casterSequence = scene.QueueCreate("Shadow caster");
    const uint64_t lightSequence =
        scene.QueueCreate("Shadow light", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::SpotLight);
    const auto created = scene.sync();
    ASSERT_EQ(created.size(), 3U);
    ASSERT_EQ(created.at(0).Sequence, receiverSequence);
    ASSERT_EQ(created.at(1).Sequence, casterSequence);
    ASSERT_EQ(created.at(2).Sequence, lightSequence);
    for (const auto& result : created) {
      ASSERT_TRUE(result.Outcome);
    }
    const Engine::EntityHandle receiver = created.at(0).Entity;
    const Engine::EntityHandle caster = created.at(1).Entity;
    const Engine::EntityHandle lightEntity = created.at(2).Entity;

    Engine::Ecs::LocalTransform receiverTransform;
    receiverTransform.Scale = {10.0F, 1.0F, 10.0F};
    (void)scene.QueueSet(receiver, receiverTransform);
    (void)scene.QueueAdd(receiver, Engine::Ecs::MeshRenderer{.PrimitiveType = "plane"});

    Engine::Ecs::LocalTransform casterTransform;
    casterTransform.Position = {0.0F, 0.9F, 0.0F};
    casterTransform.Scale = {1.2F, 1.2F, 1.2F};
    (void)scene.QueueSet(caster, casterTransform);
    (void)scene.QueueAdd(caster, Engine::Ecs::MeshRenderer{.PrimitiveType = "cube", .ReceiveShadows = false});

    Engine::Ecs::Light light;
    light.Type = lightTypes[lightIndex];
    light.Intensity = 12.0F;
    light.Range = 12.0F;
    light.Direction = light.Type == Engine::Ecs::LightType::Directional
                          ? std::array<float, 3>{0.45F, -1.0F, 0.2F}
                          : std::array<float, 3>{0.0F, -0.573462F, 0.819232F};
    light.InnerAngleDegrees = 35.0F;
    light.OuterAngleDegrees = 55.0F;
    (void)scene.QueueSet(lightEntity, light);
    Engine::Ecs::LocalTransform lightTransform;
    lightTransform.Position = light.Type == Engine::Ecs::LightType::Directional
                                  ? std::array<float, 3>{0.0F, 4.0F, -3.0F}
                                  : std::array<float, 3>{0.0F, 3.0F, -3.0F};
    (void)scene.QueueSet(lightEntity, lightTransform);
    ASSERT_TRUE(scene.Update(0.0F));

    Engine::Renderer::CameraView camera;
    camera.Position = {0.0F, 5.0F, -9.0F};
    DirectX::XMFLOAT4 cameraRotation{};
    DirectX::XMStoreFloat4(&cameraRotation, DirectX::XMQuaternionRotationRollPitchYaw(0.45F, 0.0F, 0.0F));
    camera.Rotation = {cameraRotation.x, cameraRotation.y, cameraRotation.z, cameraRotation.w};

    const auto enabledOutput = renderer.RenderFinal(scene, camera, 96, 96);
    ASSERT_TRUE(enabledOutput.has_value()) << (enabledOutput.has_value() ? "" : enabledOutput.error().Message);
    const auto enabledPixels = enabledOutput->GetPixels(Engine::RenderAov::FinalColor);
    const auto enabledNormals = enabledOutput->GetPixels(Engine::RenderAov::WorldNormal);
    ASSERT_FALSE(enabledPixels.empty());
    for (const float channel : enabledPixels) {
      EXPECT_TRUE(std::isfinite(channel)) << lightNames[lightIndex] << " produced a non-finite Ashikhmin-Shirley pixel";
    }
    ASSERT_EQ(enabledNormals.size(), enabledPixels.size() / 4U * 3U);

    receiverProperties.Model = Engine::Renderer::BrdfModel::Schlick;
    receiverMaterial->SetProperties(receiverProperties);
    const auto schlickOutput = renderer.RenderFinal(scene, camera, 96, 96);
    ASSERT_TRUE(schlickOutput.has_value()) << (schlickOutput.has_value() ? "" : schlickOutput.error().Message);
    const auto schlickPixels = schlickOutput->GetPixels(Engine::RenderAov::FinalColor);
    ASSERT_FALSE(schlickPixels.empty());
    for (const float channel : schlickPixels) {
      EXPECT_TRUE(std::isfinite(channel)) << lightNames[lightIndex] << " produced a non-finite Schlick pixel";
    }
    receiverProperties.Model = Engine::Renderer::BrdfModel::Sawicki;
    receiverMaterial->SetProperties(receiverProperties);
    const auto sawickiOutput = renderer.RenderFinal(scene, camera, 96, 96);
    ASSERT_TRUE(sawickiOutput.has_value()) << (sawickiOutput.has_value() ? "" : sawickiOutput.error().Message);
    const auto sawickiPixels = sawickiOutput->GetPixels(Engine::RenderAov::FinalColor);
    ASSERT_FALSE(sawickiPixels.empty());
    for (const float channel : sawickiPixels) {
      EXPECT_TRUE(std::isfinite(channel)) << lightNames[lightIndex] << " produced a non-finite Sawicki pixel";
    }
    receiverProperties.Model = Engine::Renderer::BrdfModel::AshikhminShirley;
    receiverMaterial->SetProperties(receiverProperties);

    light.CastShadows = false;
    (void)scene.QueueSet(lightEntity, light);
    ASSERT_TRUE(scene.Update(0.0F));
    const auto disabledOutput = renderer.RenderFinal(scene, camera, 96, 96);
    ASSERT_TRUE(disabledOutput.has_value()) << (disabledOutput.has_value() ? "" : disabledOutput.error().Message);
    const auto disabledPixels = disabledOutput->GetPixels(Engine::RenderAov::FinalColor);
    ASSERT_EQ(enabledPixels.size(), disabledPixels.size());

    size_t changedPixels = 0;
    size_t darkenedPixels = 0;
    float largestChannelDifference = 0.0F;
    float largestDarkening = 0.0F;
    for (size_t pixel = 0; pixel + 3U < enabledPixels.size(); pixel += 4U) {
      const size_t normalOffset = (pixel / 4U) * 3U;
      if (enabledNormals[normalOffset + 1U] < 0.8F) {
        continue;
      }
      const float difference = std::max({std::abs(enabledPixels[pixel] - disabledPixels[pixel]),
                                         std::abs(enabledPixels[pixel + 1U] - disabledPixels[pixel + 1U]),
                                         std::abs(enabledPixels[pixel + 2U] - disabledPixels[pixel + 2U])});
      const float enabledLuminance =
          enabledPixels[pixel] * 0.2126F + enabledPixels[pixel + 1U] * 0.7152F + enabledPixels[pixel + 2U] * 0.0722F;
      const float disabledLuminance =
          disabledPixels[pixel] * 0.2126F + disabledPixels[pixel + 1U] * 0.7152F + disabledPixels[pixel + 2U] * 0.0722F;
      const float darkening = disabledLuminance - enabledLuminance;
      largestChannelDifference = std::max(largestChannelDifference, difference);
      largestDarkening = std::max(largestDarkening, darkening);
      if (difference > 0.05F) {
        ++changedPixels;
      }
      if (darkening > 0.05F) {
        ++darkenedPixels;
      }
    }
    EXPECT_GT(changedPixels, 4U) << lightNames[lightIndex] << " shadows should visibly darken receiver pixels";
    EXPECT_GT(largestChannelDifference, 0.1F) << lightNames[lightIndex] << " shadow contrast should be visible";
    EXPECT_GT(darkenedPixels, 4U) << lightNames[lightIndex] << " shadows should darken receiver pixels";
    EXPECT_GT(largestDarkening, 0.1F) << lightNames[lightIndex] << " shadow darkening should be visible";
  }
}

TEST(RenderFinalTests, WARPSplitSumEnvironmentAffectsDielectricAndMetalOnlyWhenEnabledAndIsDeterministic) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }
  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  Engine::Renderer::MaterialProperties dielectricProperties;
  dielectricProperties.BaseColor = {0.75F, 0.4F, 0.2F, 1.0F};
  dielectricProperties.Roughness = 0.35F;
  auto dielectricMaterial = renderer.CreateMaterial(dielectricProperties);
  ASSERT_NE(dielectricMaterial, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Sphere, dielectricMaterial));

  Engine::Renderer::MaterialProperties metalProperties;
  metalProperties.BaseColor = {0.8F, 0.7F, 0.5F, 1.0F};
  metalProperties.Metallic = 1.0F;
  metalProperties.Roughness = 0.25F;
  auto metalMaterial = renderer.CreateMaterial(metalProperties);
  ASSERT_NE(metalMaterial, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Cube, metalMaterial));

  Engine::Scene scene;
  (void)scene.QueueCreate("Dielectric sphere");
  (void)scene.QueueCreate("Metal cube");
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 2U);
  ASSERT_TRUE(created.at(0).Outcome);
  ASSERT_TRUE(created.at(1).Outcome);
  (void)scene.QueueAdd(created.at(0).Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "sphere"});
  (void)scene.QueueAdd(created.at(1).Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "cube"});
  Engine::Ecs::LocalTransform dielectricTransform;
  dielectricTransform.Position = {-1.1F, 0.0F, 0.0F};
  dielectricTransform.Scale = {1.3F, 1.3F, 1.3F};
  (void)scene.QueueSet(created.at(0).Entity, dielectricTransform);
  Engine::Ecs::LocalTransform metalTransform;
  metalTransform.Position = {1.1F, 0.0F, 0.0F};
  metalTransform.Scale = {1.3F, 1.3F, 1.3F};
  (void)scene.QueueSet(created.at(1).Entity, metalTransform);
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -6.0F};
  constexpr uint32_t imageSize = 64;
  const Engine::EnvironmentLightingSettings disabled;
  const Engine::EnvironmentLightingSettings studio{Engine::EnvironmentPreset::Studio, 1.0F};
  const auto disabledFirst = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(disabledFirst.has_value()) << (disabledFirst.has_value() ? "" : disabledFirst.error().Message);
  const auto disabledSecond = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(disabledSecond.has_value()) << (disabledSecond.has_value() ? "" : disabledSecond.error().Message);
  const auto enabledFirst = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, studio);
  ASSERT_TRUE(enabledFirst.has_value()) << (enabledFirst.has_value() ? "" : enabledFirst.error().Message);
  const auto enabledSecond = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, studio);
  ASSERT_TRUE(enabledSecond.has_value()) << (enabledSecond.has_value() ? "" : enabledSecond.error().Message);

  const auto disabledDiffuse = disabledFirst->GetPixels(Engine::RenderAov::DiffuseLighting);
  const auto disabledSpecular = disabledFirst->GetPixels(Engine::RenderAov::SpecularLighting);
  const auto enabledDiffuse = enabledFirst->GetPixels(Engine::RenderAov::DiffuseLighting);
  const auto enabledSpecular = enabledFirst->GetPixels(Engine::RenderAov::SpecularLighting);
  ASSERT_EQ(disabledDiffuse.size(), enabledDiffuse.size());
  ASSERT_EQ(disabledSpecular.size(), enabledSpecular.size());
  EXPECT_TRUE(std::ranges::equal(disabledFirst->GetPixels(Engine::RenderAov::FinalColor),
                                 disabledSecond->GetPixels(Engine::RenderAov::FinalColor)));
  EXPECT_TRUE(std::ranges::equal(enabledFirst->GetPixels(Engine::RenderAov::FinalColor),
                                 enabledSecond->GetPixels(Engine::RenderAov::FinalColor)));
  const auto totalDifference = [](std::span<const float> withEnvironment,
                                  std::span<const float> withoutEnvironment) -> float {
    float difference = 0.0F;
    for (size_t channel = 0; channel < withEnvironment.size(); ++channel) {
      difference += std::abs(withEnvironment[channel] - withoutEnvironment[channel]);
    }
    return difference;
  };
  EXPECT_GT(totalDifference(enabledDiffuse, disabledDiffuse), 1.0F);
  EXPECT_GT(totalDifference(enabledSpecular, disabledSpecular), 1.0F);
  EXPECT_EQ(disabled.Preset, Engine::EnvironmentPreset::Disabled);
}

TEST(RenderFinalTests, WARPPrismNdfDirectAndEnvironmentLightingUseGeneratedPayload) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }
  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  auto payload = std::make_shared<Engine::PrismNdfData>();
  payload->zernikeCoefficients[0] = 1.0F;
  payload->normalization = 1.0F / Engine::Renderer::Brdf::Pi;
  Engine::Renderer::MaterialProperties properties;
  properties.Model = Engine::Renderer::BrdfModel::PrismNdf;
  properties.Roughness = 0.4F;
  properties.GeneratedNdf = payload;
  auto material = renderer.CreateMaterial(properties);
  ASSERT_NE(material, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Sphere, material));

  Engine::Scene scene;
  const auto sphereSequence = scene.QueueCreate("PRISM generated NDF sphere");
  const auto lightSequence =
      scene.QueueCreate("PRISM directional test light", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::SpotLight);
  const auto lightCreated = scene.sync();
  ASSERT_EQ(lightCreated.size(), 2U);
  ASSERT_EQ(lightCreated.front().Sequence, sphereSequence);
  ASSERT_EQ(lightCreated.back().Sequence, lightSequence);
  ASSERT_TRUE(lightCreated.front().Outcome);
  ASSERT_TRUE(lightCreated.back().Outcome);
  (void)scene.QueueAdd(lightCreated.front().Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "sphere"});
  Engine::Ecs::Light light;
  light.Type = Engine::Ecs::LightType::Directional;
  light.Direction = {0.0F, 0.0F, 1.0F};
  light.Intensity = 2.0F;
  light.CastShadows = false;
  (void)scene.QueueSet(lightCreated.back().Entity, light);
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -6.0F};
  constexpr uint32_t imageSize = 64;
  const auto direct = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(direct.has_value()) << (direct.has_value() ? "" : direct.error().Message);
  const auto directSpecular = direct->GetPixels(Engine::RenderAov::SpecularLighting);
  const float directEnergy = std::accumulate(directSpecular.begin(), directSpecular.end(), 0.0F);
  EXPECT_GT(directEnergy, 0.0F);

  auto zeroPayload = std::make_shared<Engine::PrismNdfData>(*payload);
  zeroPayload->zernikeCoefficients.fill(0.0F);
  properties.GeneratedNdf = zeroPayload;
  material->SetProperties(properties);
  const auto cleared = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(cleared.has_value()) << (cleared.has_value() ? "" : cleared.error().Message);
  const auto clearedSpecular = cleared->GetPixels(Engine::RenderAov::SpecularLighting);
  ASSERT_EQ(clearedSpecular.size(), directSpecular.size());
  float payloadDifference = 0.0F;
  for (size_t index = 0; index < directSpecular.size(); ++index) {
    payloadDifference += std::abs(directSpecular[index] - clearedSpecular[index]);
  }
  EXPECT_GT(payloadDifference, 1.0e-3F);

  auto residualPayload = std::make_shared<Engine::PrismNdfData>(*zeroPayload);
  residualPayload->residuals.fill(0.5F);
  properties.GeneratedNdf = residualPayload;
  material->SetProperties(properties);
  const auto residualOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(residualOutput.has_value())
      << (residualOutput.has_value() ? "" : residualOutput.error().Message);
  const auto residualSpecular = residualOutput->GetPixels(Engine::RenderAov::SpecularLighting);
  float residualEnergy = std::accumulate(residualSpecular.begin(), residualSpecular.end(), 0.0F);
  EXPECT_GT(residualEnergy, 1.0e-3F);

  properties.GeneratedNdf = payload;
  material->SetProperties(properties);
  const Engine::EnvironmentLightingSettings studio{Engine::EnvironmentPreset::Studio, 1.0F};
  const auto withEnvironment = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, studio);
  const auto repeatedEnvironment = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, studio);
  ASSERT_TRUE(withEnvironment.has_value())
      << (withEnvironment.has_value() ? "" : withEnvironment.error().Message);
  ASSERT_TRUE(repeatedEnvironment.has_value())
      << (repeatedEnvironment.has_value() ? "" : repeatedEnvironment.error().Message);
  const auto environmentSpecular = withEnvironment->GetPixels(Engine::RenderAov::SpecularLighting);
  const auto repeatedSpecular = repeatedEnvironment->GetPixels(Engine::RenderAov::SpecularLighting);
  ASSERT_EQ(environmentSpecular.size(), directSpecular.size());
  ASSERT_EQ(repeatedSpecular.size(), environmentSpecular.size());
  float environmentDifference = 0.0F;
  for (size_t index = 0; index < environmentSpecular.size(); ++index) {
    environmentDifference += std::abs(environmentSpecular[index] - directSpecular[index]);
  }
  EXPECT_GT(environmentDifference, 1.0e-3F);
  EXPECT_TRUE(std::ranges::equal(environmentSpecular, repeatedSpecular));
}

TEST(RenderFinalTests, WARPPrismDirectAndConstantEnvironmentMatchIndependentReferencesAndRefreshBindings) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }

  const auto project =
      std::filesystem::temp_directory_path() /
      ("GraphicEnginePrismReference-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(project);
  WriteConstantHdr(project / "constant-bright.hdr", 128);
  WriteConstantHdr(project / "constant-dim.hdr", 64);
  Engine::AssetManager assets(project);
  const auto brightGuid = assets.ImportEnvironment("constant-bright.hdr");
  const auto dimGuid = assets.ImportEnvironment("constant-dim.hdr");
  ASSERT_TRUE(brightGuid.has_value()) << (brightGuid.has_value() ? "" : brightGuid.error().Message);
  ASSERT_TRUE(dimGuid.has_value()) << (dimGuid.has_value() ? "" : dimGuid.error().Message);

  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());
  renderer.SetAssetManager(&assets);
  std::string error;
  ASSERT_TRUE(renderer.PrepareAssetEnvironment(*brightGuid, error)) << error;
  ASSERT_TRUE(renderer.PrepareAssetEnvironment(*dimGuid, error)) << error;

  auto payload = std::make_shared<Engine::PrismNdfData>();
  payload->zernikeCoefficients[0] = 1.0F;
  payload->normalization = 1.0F / Engine::Renderer::Brdf::Pi;
  Engine::Renderer::MaterialProperties properties;
  properties.Model = Engine::Renderer::BrdfModel::PrismNdf;
  properties.Roughness = 0.4F;
  properties.SpecularMode = Engine::Renderer::DielectricSpecularMode::ExplicitF0;
  properties.SpecularF0 = {0.04F, 0.04F, 0.04F};
  properties.GeneratedNdf = payload;
  auto material = renderer.CreateMaterial(properties);
  ASSERT_NE(material, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Sphere, material));

  Engine::Scene scene;
  const auto sphereSequence = scene.QueueCreate("PRISM reference sphere");
  const auto lightSequence =
      scene.QueueCreate("PRISM reference light", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::SpotLight);
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 2U);
  ASSERT_EQ(created.at(0).Sequence, sphereSequence);
  ASSERT_EQ(created.at(1).Sequence, lightSequence);
  ASSERT_TRUE(created.at(0).Outcome);
  ASSERT_TRUE(created.at(1).Outcome);
  (void)scene.QueueAdd(created.at(0).Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "sphere"});
  Engine::Ecs::Light light;
  light.Type = Engine::Ecs::LightType::Directional;
  light.Direction = {0.0F, 0.0F, 1.0F};
  light.Intensity = 2.0F;
  light.CastShadows = false;
  (void)scene.QueueSet(created.at(1).Entity, light);
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -6.0F};
  constexpr uint32_t imageSize = 65;
  constexpr double f0 = 0.04;
  constexpr double roughness = 0.4;
  constexpr double normalization = 1.0 / std::numbers::pi;
  constexpr double directReference = f0 * normalization * 2.0 / 4.0;
  const auto directOnly = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(directOnly.has_value()) << (directOnly.has_value() ? "" : directOnly.error().Message);
  const auto directSpecular = CenterSpecular(*directOnly, imageSize, imageSize);
  EXPECT_NEAR(directSpecular[0], directReference, 1.0e-4);
  EXPECT_NEAR(directSpecular[1], directReference, 1.0e-4);
  EXPECT_NEAR(directSpecular[2], directReference, 1.0e-4);

  Engine::ImportedImage whiteBaseColor{.name = "PRISM reference white base color",
                                       .mimeType = "image/raw",
                                       .width = 1,
                                       .height = 1,
                                       .rgba8 = {255, 255, 255, 255}};
  auto baseColorTexture = renderer.GetOrCreateTexture("prism-reference-base-color", 1U, whiteBaseColor, true, error);
  ASSERT_NE(baseColorTexture, nullptr) << error;
  ASSERT_TRUE(renderer.SetMaterialTextures(material, std::move(baseColorTexture), nullptr));
  const auto texturedDirect = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(texturedDirect.has_value()) << (texturedDirect.has_value() ? "" : texturedDirect.error().Message);
  const auto texturedDirectSpecular = CenterSpecular(*texturedDirect, imageSize, imageSize);
  for (size_t channel = 0; channel < directSpecular.size(); ++channel) {
    EXPECT_NEAR(texturedDirectSpecular[channel], directReference, 1.0e-4) << "textured direct channel=" << channel;
  }

  const auto skyboxSequence =
      scene.QueueAdd(scene.SceneRootEntity(), Engine::Ecs::Skybox{.EnvironmentGuid = *brightGuid});
  const auto skyboxAdded = scene.sync();
  ASSERT_EQ(skyboxAdded.size(), 1U);
  ASSERT_EQ(skyboxAdded.front().Sequence, skyboxSequence);
  ASSERT_TRUE(skyboxAdded.front().Outcome);
  const auto changeSkybox = [&scene](const std::string& guid) {
    const uint64_t sequence = scene.QueueSet(scene.SceneRootEntity(), Engine::Ecs::Skybox{.EnvironmentGuid = guid});
    const auto results = scene.sync();
    return results.size() == 1U && results.front().Sequence == sequence && static_cast<bool>(results.front().Outcome);
  };
  const auto bright = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(bright.has_value()) << (bright.has_value() ? "" : bright.error().Message);
  const auto brightSpecular = CenterSpecular(*bright, imageSize, imageSize);
  const std::array<double, 3> brightRadiance{2.0, 1.0, 0.5};
  constexpr uint32_t referenceSamples = 1U << 20U;
  const auto brightReference =
      HighSampleConstantEnvironmentReference(brightRadiance, normalization, f0, roughness, referenceSamples);
  for (size_t channel = 0; channel < brightSpecular.size(); ++channel) {
    EXPECT_NEAR(static_cast<double>(brightSpecular[channel] - directSpecular[channel]), brightReference[channel],
                2.0e-3 + 5.0e-3 * std::abs(brightReference[channel]))
        << "bright environment channel=" << channel;
  }

  auto updatedPayload = std::make_shared<Engine::PrismNdfData>(*payload);
  updatedPayload->normalization *= 2.0F;
  properties.GeneratedNdf = updatedPayload;
  material->SetProperties(properties);
  const auto updatedNdf = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(updatedNdf.has_value()) << (updatedNdf.has_value() ? "" : updatedNdf.error().Message);
  const auto updatedNdfSpecular = CenterSpecular(*updatedNdf, imageSize, imageSize);
  for (size_t channel = 0; channel < updatedNdfSpecular.size(); ++channel) {
    EXPECT_NEAR(static_cast<double>(updatedNdfSpecular[channel] - directSpecular[channel]),
                directReference + brightReference[channel] * 2.0,
                2.0e-3 + 5.0e-3 * std::abs(directReference + brightReference[channel] * 2.0))
        << "updated NDF channel=" << channel;
  }

  ASSERT_TRUE(changeSkybox(*dimGuid));
  const auto dim = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(dim.has_value()) << (dim.has_value() ? "" : dim.error().Message);
  const auto dimSpecular = CenterSpecular(*dim, imageSize, imageSize);
  const std::array<double, 3> dimRadiance{1.0, 0.5, 0.25};
  const auto dimReference =
      HighSampleConstantEnvironmentReference(dimRadiance, normalization * 2.0, f0, roughness, referenceSamples);
  for (size_t channel = 0; channel < dimSpecular.size(); ++channel) {
    EXPECT_NEAR(static_cast<double>(dimSpecular[channel] - directSpecular[channel]),
                directReference + dimReference[channel],
                2.0e-3 + 5.0e-3 * std::abs(directReference + dimReference[channel]))
        << "updated environment channel=" << channel;
  }
  std::filesystem::remove_all(project);
}

TEST(RenderFinalTests, WARPFinalRenderRejectsInvalidAmbientOverrideBeforeStartingTheFrame) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }
  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  Engine::Scene scene;
  Engine::Renderer::CameraView camera;
  constexpr uint32_t imageSize = 16;
  const std::array invalidOverrides = {
      Engine::AmbientLightSettings{.Intensity = -0.1F},
      Engine::AmbientLightSettings{.Color = {std::numeric_limits<float>::quiet_NaN(), 1.0F, 1.0F}},
  };
  for (const Engine::AmbientLightSettings& ambientLight : invalidOverrides) {
    const auto result = renderer.BeginFinalRender(scene, camera, imageSize, imageSize, {}, {}, {}, ambientLight);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message.find("ambient light override"), std::string::npos);
    EXPECT_FALSE(renderer.IsFrameActive());
  }

  const auto valid = renderer.RenderFinal(scene, camera, imageSize, imageSize);
  ASSERT_TRUE(valid.has_value()) << (valid.has_value() ? "" : valid.error().Message);
}

TEST(RenderFinalTests, WARPScreenSpaceAmbientOcclusionOnlyChangesIndirectDiffuseLighting) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }
  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());

  Engine::Renderer::MaterialProperties receiverProperties;
  receiverProperties.BaseColor = {0.7F, 0.6F, 0.5F, 1.0F};
  receiverProperties.Emissive = {0.05F, 0.02F, 0.01F};
  const auto receiverMaterial = renderer.CreateMaterial(receiverProperties);
  ASSERT_NE(receiverMaterial, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Cube, receiverMaterial));

  Engine::Renderer::MaterialProperties occluderProperties;
  occluderProperties.BaseColor = {0.4F, 0.5F, 0.8F, 1.0F};
  const auto occluderMaterial = renderer.CreateMaterial(occluderProperties);
  ASSERT_NE(occluderMaterial, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Sphere, occluderMaterial));

  Engine::Scene scene;
  (void)scene.QueueCreate("Ambient receiver");
  (void)scene.QueueCreate("Ambient occluder");
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 2U);
  ASSERT_TRUE(created.at(0).Outcome);
  ASSERT_TRUE(created.at(1).Outcome);
  (void)scene.QueueAdd(created.at(0).Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "cube"});
  (void)scene.QueueAdd(created.at(1).Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "sphere"});
  Engine::Ecs::LocalTransform receiverTransform;
  receiverTransform.Scale = {4.0F, 4.0F, 0.5F};
  (void)scene.QueueSet(created.at(0).Entity, receiverTransform);
  Engine::Ecs::LocalTransform occluderTransform;
  occluderTransform.Position = {0.0F, 0.0F, 0.2F};
  occluderTransform.Scale = {0.5F, 0.5F, 0.5F};
  (void)scene.QueueSet(created.at(1).Entity, occluderTransform);
  ASSERT_TRUE(scene.Update(0.0F));

  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -6.0F};
  constexpr uint32_t imageSize = 64;
  const auto baseline = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {});
  ASSERT_TRUE(baseline.has_value()) << (baseline.has_value() ? "" : baseline.error().Message);
  const Engine::Ecs::CameraPostProcessing ambientOcclusion{
      .AmbientOcclusion = Engine::Ecs::ScreenSpaceAmbientOcclusionSettings{.Radius = 2.0F, .Intensity = 4.0F}};
  const auto occluded = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, ambientOcclusion);
  ASSERT_TRUE(occluded.has_value()) << (occluded.has_value() ? "" : occluded.error().Message);

  const auto totalDifference = [](std::span<const float> left, std::span<const float> right) -> float {
    float difference = 0.0F;
    for (size_t index = 0; index < left.size(); ++index) {
      difference += std::abs(left[index] - right[index]);
    }
    return difference;
  };
  EXPECT_GT(totalDifference(occluded->GetPixels(Engine::RenderAov::DiffuseLighting),
                            baseline->GetPixels(Engine::RenderAov::DiffuseLighting)),
            0.01F);
  EXPECT_TRUE(std::ranges::equal(occluded->GetPixels(Engine::RenderAov::SpecularLighting),
                                 baseline->GetPixels(Engine::RenderAov::SpecularLighting)));
  EXPECT_TRUE(
      std::ranges::equal(occluded->GetPixels(Engine::RenderAov::Depth), baseline->GetPixels(Engine::RenderAov::Depth)));
  EXPECT_TRUE(std::ranges::equal(occluded->GetPixels(Engine::RenderAov::WorldNormal),
                                 baseline->GetPixels(Engine::RenderAov::WorldNormal)));
  EXPECT_GT(totalDifference(occluded->GetPixels(Engine::RenderAov::FinalColor),
                            baseline->GetPixels(Engine::RenderAov::FinalColor)),
            0.01F);

  const Engine::Ecs::CameraPostProcessing lowSampleCount{
      .AmbientOcclusion =
          Engine::Ecs::ScreenSpaceAmbientOcclusionSettings{.Radius = 2.0F, .Intensity = 4.0F, .SampleCount = 8}};
  const Engine::Ecs::CameraPostProcessing highSampleCount{
      .AmbientOcclusion =
          Engine::Ecs::ScreenSpaceAmbientOcclusionSettings{.Radius = 2.0F, .Intensity = 4.0F, .SampleCount = 32}};
  const Engine::Ecs::CameraPostProcessing highBias{
      .AmbientOcclusion = Engine::Ecs::ScreenSpaceAmbientOcclusionSettings{.Radius = 2.0F,
                                                                           .Intensity = 4.0F,
                                                                           .SampleCount = 32,
                                                                           .DepthBias = 0.1F}};
  const auto lowSampleOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, lowSampleCount);
  const auto highSampleOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, highSampleCount);
  const auto highBiasOutput = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, highBias);
  ASSERT_TRUE(lowSampleOutput.has_value());
  ASSERT_TRUE(highSampleOutput.has_value());
  ASSERT_TRUE(highBiasOutput.has_value());
  EXPECT_GT(totalDifference(lowSampleOutput->GetPixels(Engine::RenderAov::DiffuseLighting),
                            highSampleOutput->GetPixels(Engine::RenderAov::DiffuseLighting)),
            0.001F);
  EXPECT_GT(totalDifference(highSampleOutput->GetPixels(Engine::RenderAov::DiffuseLighting),
                            highBiasOutput->GetPixels(Engine::RenderAov::DiffuseLighting)),
            0.001F);

  const Engine::AmbientLightSettings ambientDisabled{.Enabled = false};
  const auto noAmbient = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, {}, ambientDisabled);
  ASSERT_TRUE(noAmbient.has_value()) << (noAmbient.has_value() ? "" : noAmbient.error().Message);
  const auto occludedWithoutAmbient =
      renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, ambientOcclusion, ambientDisabled);
  ASSERT_TRUE(occludedWithoutAmbient.has_value())
      << (occludedWithoutAmbient.has_value() ? "" : occludedWithoutAmbient.error().Message);
  EXPECT_TRUE(std::ranges::equal(occludedWithoutAmbient->GetPixels(Engine::RenderAov::DiffuseLighting),
                                 noAmbient->GetPixels(Engine::RenderAov::DiffuseLighting)));

  const Engine::AmbientLightSettings redAmbient{.Color = {1.0F, 0.0F, 0.0F}, .Intensity = 0.4F};
  const Engine::AmbientLightSettings blueAmbient{.Color = {0.0F, 0.0F, 1.0F}, .Intensity = 0.4F};
  const auto redCamera = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, {}, redAmbient);
  const auto blueCamera = renderer.RenderFinal(scene, camera, imageSize, imageSize, {}, {}, {}, blueAmbient);
  ASSERT_TRUE(redCamera.has_value()) << (redCamera.has_value() ? "" : redCamera.error().Message);
  ASSERT_TRUE(blueCamera.has_value()) << (blueCamera.has_value() ? "" : blueCamera.error().Message);
  EXPECT_GT(totalDifference(redCamera->GetPixels(Engine::RenderAov::DiffuseLighting),
                            blueCamera->GetPixels(Engine::RenderAov::DiffuseLighting)),
            0.01F);
  EXPECT_TRUE(std::ranges::equal(redCamera->GetPixels(Engine::RenderAov::SpecularLighting),
                                 blueCamera->GetPixels(Engine::RenderAov::SpecularLighting)));
}
} // namespace
