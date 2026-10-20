#include "panels/include/AssetDragDropPayload.h"
#include "panels/include/AssetDropAssignment.h"
#include "rendering/include/RenderSystem.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/src/SceneRenderer.h"
#include "rendering/src/render_graph/RenderGraph.h"
#include "rendering/src/render_graph/RenderView.h"
#include "scene/include/Scene.h"
#include "scene/include/components/EcsComponents.h"

#include "../../support/WarpRenderBackend.h"

#include <gtest/gtest.h>

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

  std::vector<std::tuple<std::string, bool, bool, bool>> resolved;
  renderer.GetSceneRenderer()->SetAssetTextureResolver(
      [&resolved, &gpuTextures](const std::string& guid, bool srgb, bool normalMap,
                                bool preserveAlphaCoverage) -> std::shared_ptr<Engine::Renderer::TextureResource> {
        resolved.emplace_back(guid, srgb, normalMap, preserveAlphaCoverage);
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
  EXPECT_EQ(resolved[0], (std::tuple<std::string, bool, bool, bool>{baseColorGuid, true, false, false}));
  const auto baseColorWarmOutput = renderer.RenderFinal(scene, camera, 32, 32);
  ASSERT_TRUE(baseColorWarmOutput.has_value())
      << (baseColorWarmOutput.has_value() ? "" : baseColorWarmOutput.error().Message);

  resolved.clear();
  (void)scene.QueueSet(meshEntity, Engine::Ecs::MeshRenderer{.PrimitiveType = "cube", .Material = material});
  ASSERT_TRUE(scene.Update(0.0F));
  const auto output = renderer.RenderFinal(scene, camera, 32, 32);
  ASSERT_TRUE(output.has_value()) << (output.has_value() ? "" : output.error().Message);
  ASSERT_EQ(resolved.size(), 2U);
  EXPECT_EQ(resolved[0], (std::tuple<std::string, bool, bool, bool>{baseColorGuid, true, false, false}));
  EXPECT_EQ(resolved[1], (std::tuple<std::string, bool, bool, bool>{normalGuid, false, true, false}));
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
} // namespace
