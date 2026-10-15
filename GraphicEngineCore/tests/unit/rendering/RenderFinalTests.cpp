#include "rendering/include/RenderSystem.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/src/render_graph/RenderGraph.h"
#include "scene/include/Scene.h"
#include "scene/include/components/EcsComponents.h"

#include "../../support/WarpRenderBackend.h"

#include <gtest/gtest.h>

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
} // namespace
