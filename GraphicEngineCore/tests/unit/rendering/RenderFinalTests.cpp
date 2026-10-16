#include "rendering/include/RenderSystem.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/src/render_graph/RenderGraph.h"
#include "rendering/src/render_graph/RenderView.h"
#include "scene/include/Scene.h"
#include "scene/include/components/EcsComponents.h"

#include "../../support/WarpRenderBackend.h"

#include <gtest/gtest.h>

#include <DirectXMath.h>
#include <array>
#include <cstddef>

#include <string>

namespace {
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
} // namespace
