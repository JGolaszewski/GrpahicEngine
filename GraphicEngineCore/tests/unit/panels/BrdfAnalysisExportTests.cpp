#include "panels/include/BrdfAnalysisExport.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4702)
#endif
#include <OpenImageIO/imageio.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

class TemporaryExportDirectory final {
public:
  TemporaryExportDirectory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    m_Root = std::filesystem::temp_directory_path() / ("GraphicEngineBrdfExports-" + std::to_string(nonce));
    std::filesystem::create_directories(m_Root);
  }

  TemporaryExportDirectory(const TemporaryExportDirectory&) = delete;
  auto operator=(const TemporaryExportDirectory&) -> TemporaryExportDirectory& = delete;
  ~TemporaryExportDirectory() noexcept {
    std::error_code ignored;
    try {
      std::filesystem::remove_all(m_Root, ignored);
    } catch (...) {
      std::fputs("BRDF export test workspace cleanup failed\n", stderr);
    }
  }

  [[nodiscard]] auto File(const char* name) const -> std::filesystem::path { return m_Root / name; }

private:
  std::filesystem::path m_Root;
};

auto MakeComparison() -> BrdfAnalysisPanel::CompletedComparison {
  BrdfAnalysisPanel::CompletedComparison comparison;
  comparison.TargetLabel = "Sphere (entity material)";
  comparison.Snapshot.TargetEntityId = 7;
  comparison.Snapshot.Width = 640;
  comparison.Snapshot.Height = 360;
  comparison.Snapshot.ShadowMapResolution = 2048;
  comparison.Snapshot.MaxShadowCastersPerScene = 4;
  comparison.Snapshot.Camera.Position = {1.0F, 2.0F, 3.0F};
  comparison.Snapshot.Camera.FieldOfViewDegrees = 50.0F;
  comparison.Snapshot.SourceMaterial.brdfModel = "ggx";
  comparison.Snapshot.SourceMaterial.roughness = 0.35F;
  comparison.Snapshot.SourceMaterialProperties.Roughness = 0.35F;
  Engine::SceneEntityDocument entity;
  entity.id = 7;
  entity.name = "Sphere";
  entity.primitiveType = "sphere";
  entity.material = comparison.Snapshot.SourceMaterial;
  comparison.Snapshot.Scene.entities.push_back(entity);

  BrdfAnalysisPanel::VariantResult result;
  result.Curve.Model = Engine::Renderer::BrdfModel::GGX;
  result.Curve.AnglesDegrees = {0.0F, 45.0F, 90.0F};
  result.Curve.Response = {0.2F, 0.8F, 0.3F};
  result.Curve.Measurements = {.Peak = 0.8F, .Mean = 0.43333334F, .Integral = 0.7F};
  result.SceneLuminance = {.Mean = 0.4F, .Peak = 3.25F};
  result.Render.Request.Model = Engine::Renderer::BrdfModel::GGX;
  result.Render.Request.Width = 640;
  result.Render.Request.Height = 360;
  result.Render.Request.ShadowMapResolution = 2048;
  result.Render.Request.MaxShadowCastersPerScene = 4;
  comparison.Variants.push_back(std::move(result));
  return comparison;
}

TEST(BrdfAnalysisExportTests, WritesChartPngForDisplayedCurves) {
  TemporaryExportDirectory directory;
  const auto comparison = MakeComparison();
  const auto path = directory.File("comparison.png");

  const Engine::Status result = BrdfAnalysisExporter::WriteChartPng(comparison, path);
  ASSERT_TRUE(result) << (result ? "" : result.error().Message);
  auto input = OIIO::ImageInput::open(path.wstring());
  ASSERT_NE(input, nullptr) << OIIO::geterror();
  EXPECT_EQ(input->spec().width, 1200);
  EXPECT_EQ(input->spec().height, 720);
  EXPECT_EQ(input->spec().nchannels, 4);
  std::vector<uint8_t> pixels(static_cast<std::size_t>(1200U) * 720U * 4U);
  ASSERT_TRUE(input->read_image(0, 0, 0, -1, OIIO::span<uint8_t>(pixels.data(), pixels.size()))) << input->geterror();
  EXPECT_EQ(pixels.at(3), 255);
}

TEST(BrdfAnalysisExportTests, WritesSceneSettingsAndMeasurementsToJson) {
  TemporaryExportDirectory directory;
  const auto comparison = MakeComparison();
  const auto path = directory.File("comparison.json");

  const Engine::Status result = BrdfAnalysisExporter::WriteJson(comparison, path);
  ASSERT_TRUE(result) << (result ? "" : result.error().Message);
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input.good());
  const auto json = nlohmann::json::parse(input);
  EXPECT_EQ(json.at("scene").at("entities").at(0).at("name"), "Sphere");
  EXPECT_EQ(json.at("material").at("source").at("brdfModel"), "ggx");
  EXPECT_FLOAT_EQ(json.at("camera").at("fieldOfViewDegrees").get<float>(), 50.0F);
  EXPECT_EQ(json.at("render").at("width"), 640U);
  EXPECT_EQ(json.at("render").at("shadowMapResolution"), 2048U);
  EXPECT_EQ(json.at("render").at("maxShadowCastersPerScene"), 4U);
  EXPECT_FLOAT_EQ(json.at("variants").at(0).at("curve").at("measurements").at("peak").get<float>(), 0.8F);
  EXPECT_FLOAT_EQ(json.at("variants").at(0).at("sceneLuminance").at("peak").get<float>(), 3.25F);
  EXPECT_EQ(json.at("variants").at(0).at("curve").at("anglesDegrees").size(), 3U);
}

TEST(BrdfAnalysisExportTests, RejectsMalformedCurvesAndWrongExtensions) {
  TemporaryExportDirectory directory;
  auto comparison = MakeComparison();
  comparison.Variants.front().Curve.Response.pop_back();
  EXPECT_FALSE(BrdfAnalysisExporter::WriteChartPng(comparison, directory.File("bad.png")));
  comparison = MakeComparison();
  EXPECT_FALSE(BrdfAnalysisExporter::WriteChartPng(comparison, directory.File("bad.jpg")));
  EXPECT_FALSE(BrdfAnalysisExporter::WriteJson(comparison, directory.File("bad.txt")));
}

} // namespace
