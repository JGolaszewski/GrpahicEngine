#include "rendering/include/RenderOutputExportService.h"
#include "rendering/include/renderer/CameraView.h"
#include "scene/include/Scene.h"

#include "../../support/WarpRenderBackend.h"

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4702) // OpenImageIO's template code triggers MSVC's unreachable-code warning.
#endif
#include <OpenImageIO/imageio.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <ranges>
#include <string>
#include <thread>
#include <vector>

namespace {

class TemporaryOutputDirectory final {
public:
  TemporaryOutputDirectory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    m_Root = std::filesystem::temp_directory_path() / ("GraphicEngineAsyncExportTests-" + std::to_string(nonce));
    std::filesystem::create_directories(m_Root);
  }
  ~TemporaryOutputDirectory() noexcept { // NOLINT(bugprone-exception-escape): uses remove_all's error_code overload.
    std::error_code ignored;
    std::filesystem::remove_all(m_Root, ignored);
  }
  [[nodiscard]] auto File(const char* name) const -> std::filesystem::path { return m_Root / name; }

private:
  std::filesystem::path m_Root;
};

auto FinishExport(Engine::RenderOutputExportService& service, Engine::RenderOutputExportService::RequestId id)
    -> Engine::RenderOutputExportStatus {
  constexpr auto timeout = std::chrono::seconds(10);
  const auto start = std::chrono::steady_clock::now();
  for (;;) {
    service.Update();
    const auto status = service.GetStatus(id);
    if (!status.has_value()) {
      ADD_FAILURE() << "export request disappeared";
      return {};
    }
    if (status->Phase == Engine::RenderOutputExportPhase::Completed ||
        status->Phase == Engine::RenderOutputExportPhase::Failed) {
      return *status;
    }
    if (std::chrono::steady_clock::now() - start >= timeout) {
      ADD_FAILURE() << "asynchronous export did not complete before timeout";
      return *status;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

TEST(RenderOutputExportServiceTests, WritesPngAsynchronouslyAtRequestedDimensions) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }
  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  Engine::Scene scene;
  Engine::Renderer::CameraView camera;
  Engine::RenderOutputExportService exports;
  TemporaryOutputDirectory directory;
  const auto path = directory.File("camera.png");

  auto started = exports.StartPng(renderer, scene, camera, 13, 7, path);
  ASSERT_TRUE(started) << (started ? "" : started.error().Message);
  auto initial = exports.GetStatus(*started);
  ASSERT_TRUE(initial.has_value());
  EXPECT_EQ(initial->Phase, Engine::RenderOutputExportPhase::WaitingForReadback);
  EXPECT_FALSE(std::filesystem::exists(path));

  const auto status = FinishExport(exports, *started);
  ASSERT_EQ(status.Phase, Engine::RenderOutputExportPhase::Completed) << status.Message;
  EXPECT_EQ(status.Progress, 1.0F);
  EXPECT_TRUE(std::filesystem::exists(path));

  auto input = OIIO::ImageInput::open(path.wstring());
  ASSERT_NE(input, nullptr) << OIIO::geterror();
  EXPECT_EQ(input->spec().width, 13);
  EXPECT_EQ(input->spec().height, 7);
  EXPECT_EQ(input->spec().nchannels, 4);
  std::vector<float> pixels(13U * 7U * 4U);
  ASSERT_TRUE(input->read_image(0, 0, 0, -1, OIIO::span<float>(pixels.data(), pixels.size()))) << input->geterror();
  EXPECT_TRUE(std::ranges::all_of(pixels, [](float sample) { return std::isfinite(sample); }));
  EXPECT_TRUE(input->close());
}

TEST(RenderOutputExportServiceTests, WritesOnlySelectedAovsToExrAndReportsWriteFailure) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }
  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  Engine::Scene scene;
  Engine::Renderer::CameraView camera;
  Engine::RenderOutputExportService exports;
  TemporaryOutputDirectory directory;

  const auto exrPath = directory.File("selected.exr");
  auto started = exports.StartMultilayerExr(renderer, scene, camera, 8, 5,
                                            {Engine::RenderAov::Depth, Engine::RenderAov::FinalColor}, exrPath);
  ASSERT_TRUE(started) << (started ? "" : started.error().Message);
  const auto status = FinishExport(exports, *started);
  ASSERT_EQ(status.Phase, Engine::RenderOutputExportPhase::Completed) << status.Message;

  auto input = OIIO::ImageInput::open(exrPath.wstring());
  ASSERT_NE(input, nullptr) << OIIO::geterror();
  EXPECT_EQ(input->spec().width, 8);
  EXPECT_EQ(input->spec().height, 5);
  EXPECT_EQ(input->spec().nchannels, 1);
  EXPECT_EQ(input->spec().get_string_attribute("oiio:subimagename"), "depth");
  std::vector<float> depthPixels(8U * 5U);
  ASSERT_TRUE(input->read_image(0, 0, 0, -1, OIIO::span<float>(depthPixels.data(), depthPixels.size())))
      << input->geterror();
  EXPECT_TRUE(std::ranges::all_of(depthPixels, [](float sample) { return std::isfinite(sample); }));
  ASSERT_TRUE(input->seek_subimage(1, 0));
  EXPECT_EQ(input->spec().nchannels, 4);
  EXPECT_EQ(input->spec().get_string_attribute("oiio:subimagename"), "final_color");
  std::vector<float> colorPixels(8U * 5U * 4U);
  ASSERT_TRUE(input->read_image(1, 0, 0, -1, OIIO::span<float>(colorPixels.data(), colorPixels.size())))
      << input->geterror();
  EXPECT_TRUE(std::ranges::all_of(colorPixels, [](float sample) { return std::isfinite(sample); }));
  EXPECT_TRUE(input->close());

  auto failedStart = exports.StartPng(renderer, scene, camera, 4, 4, directory.File("wrong.jpg"));
  ASSERT_TRUE(failedStart) << (failedStart ? "" : failedStart.error().Message);
  const auto failure = FinishExport(exports, *failedStart);
  EXPECT_EQ(failure.Phase, Engine::RenderOutputExportPhase::Failed);
  EXPECT_NE(failure.Message.find("extension"), std::string::npos);
}

TEST(RenderOutputExportServiceTests, ShutdownCancelsGpuReadbackBeforeRendererTeardown) {
  EngineTests::WarpDevice device;
  if (!device.Initialize()) {
    GTEST_SKIP() << "D3D11 WARP device could not be created on this host";
  }
  EngineTests::WarpRenderBackend backend(device.GetDevice());
  Engine::RenderSystem renderer(&backend);
  Engine::Scene scene;
  Engine::Renderer::CameraView camera;
  Engine::RenderOutputExportService exports;

  auto started = exports.StartPng(renderer, scene, camera, 8, 8, std::filesystem::temp_directory_path() / "cancel.png");
  ASSERT_TRUE(started) << (started ? "" : started.error().Message);
  exports.Shutdown();
  const auto status = exports.GetStatus(*started);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->Phase, Engine::RenderOutputExportPhase::Failed);
  EXPECT_NE(status->Message.find("cancelled"), std::string::npos);
  EXPECT_FALSE(renderer.IsFrameActive());
}

} // namespace
