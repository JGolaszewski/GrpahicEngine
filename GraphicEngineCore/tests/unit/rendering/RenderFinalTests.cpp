#include "rendering/include/RenderSystem.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/src/render_graph/RenderGraph.h"
#include "scene/include/Scene.h"

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
} // namespace
