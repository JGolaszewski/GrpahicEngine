#include "panels/include/AnalyticalFrameArchive.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

class TemporaryArchiveDirectory final {
public:
  TemporaryArchiveDirectory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    m_Root = std::filesystem::temp_directory_path() / ("GraphicEngineAnalyticalFrame-" + std::to_string(nonce));
    std::filesystem::create_directories(m_Root);
  }

  TemporaryArchiveDirectory(const TemporaryArchiveDirectory&) = delete;
  auto operator=(const TemporaryArchiveDirectory&) -> TemporaryArchiveDirectory& = delete;
  ~TemporaryArchiveDirectory() noexcept {
    try {
      std::error_code ignored;
      std::filesystem::remove_all(m_Root, ignored);
    } catch (...) {
      std::fputs("Analytical Frame archive test cleanup failed\n", stderr);
    }
  }

  [[nodiscard]] auto File(std::string_view name) const -> std::filesystem::path { return m_Root / name; }

private:
  std::filesystem::path m_Root;
};

auto MakeViewportState() -> nlohmann::json {
  return {{"SelectedAov", "finalColor"},
          {"SelectedModel", "ggx"},
          {"Zoom", 1.25F},
          {"PanOffsetPixels", {12.0F, -3.0F}},
          {"PinnedPixel", {1U, 0U}},
          {"CompareModelA", "ggx"},
          {"CompareModelB", "gtr"},
          {"Mode", "Wipe"},
          {"WipePosition", 0.4F}};
}

auto MakeSession() -> AnalyticalFrame::AnalyticalFrameSession {
  using namespace Engine::Renderer;
  using namespace Engine::Renderer::BrdfAnalysis;
  AnalyticalFrame::AnalyticalFrameSession session;
  auto& snapshot = session.Snapshot;
  snapshot.TargetEntityId = 42;
  snapshot.SourceMaterial.brdfModel = "ggx";
  snapshot.SourceMaterial.roughness = 0.37F;
  snapshot.SourceMaterial.baseColorTextureGuid = "01234567-89ab-cdef-0123-456789abcdef";
  snapshot.SourceMaterialProperties.Model = BrdfModel::GGX;
  snapshot.SourceMaterialProperties.Roughness = 0.37F;
  snapshot.SourceMaterialProperties.TextureTransforms[0].Offset = {0.25F, 0.5F};
  snapshot.Camera.Position = {1.0F, 2.0F, 3.0F};
  snapshot.Camera.FieldOfViewDegrees = 52.0F;
  snapshot.Width = 4;
  snapshot.Height = 2;
  snapshot.ShadowMapResolution = 1024;
  snapshot.MaxShadowCastersPerScene = 3;
  snapshot.FrameOverrides.OutputResolution = BrdfAnalysisOverrides::Resolution{2, 1};
  snapshot.FrameOverrides.Roughness = 0.25F;
  snapshot.FrameOverrides.GtrGamma = 2.5F;
  Engine::SceneEntityDocument target;
  target.id = 42;
  target.name = "Captured sphere";
  target.primitiveType = "sphere";
  target.material = snapshot.SourceMaterial;
  snapshot.Scene.entities.push_back(target);

  session.TargetLabel = "Captured sphere material";
  session.ViewportState = MakeViewportState();
  session.PostProcessCallbackPresent = true;

  AnalyticalFrame::AnalyticalFrameVariant variant;
  auto& request = variant.Render.Request;
  request.Scene = snapshot.Scene;
  Engine::SceneMaterialDocument requestMaterial =
      request.Scene.entities[0].material.value_or(Engine::SceneMaterialDocument{});
  requestMaterial.brdfModel = "ggx";
  requestMaterial.roughness = 0.25F;
  requestMaterial.gtrGamma = 2.5F;
  request.Scene.entities[0].material = std::move(requestMaterial);
  request.Model = BrdfModel::GGX;
  request.Material = snapshot.SourceMaterialProperties;
  request.Material.Model = BrdfModel::GGX;
  request.Material.Roughness = 0.25F;
  request.Material.GtrGamma = 2.5F;
  request.Camera = snapshot.Camera;
  request.Width = 2;
  request.Height = 1;
  request.ShadowMapResolution = snapshot.ShadowMapResolution;
  request.MaxShadowCastersPerScene = snapshot.MaxShadowCastersPerScene;
  request.FrameOverrides = snapshot.FrameOverrides;
  std::vector<Engine::RenderOutputPlane> planes;
  for (const auto aov : {Engine::RenderAov::FinalColor, Engine::RenderAov::Depth, Engine::RenderAov::WorldNormal,
                         Engine::RenderAov::SpecularLighting, Engine::RenderAov::DiffuseLighting}) {
    const uint32_t channels = Engine::RenderOutput::GetDescriptor(aov).ChannelCount;
    std::vector<float> pixels(static_cast<size_t>(request.Width) * request.Height * channels);
    for (size_t index = 0; index < pixels.size(); ++index) {
      pixels[index] = static_cast<float>(index + static_cast<size_t>(aov) + 1U) * 0.125F;
    }
    planes.push_back({.Aov = aov, .ChannelCount = channels, .Pixels = std::move(pixels)});
  }
  auto output = Engine::RenderOutput::CreateFromPlanes(request.Width, request.Height, std::move(planes));
  EXPECT_TRUE(output.has_value());
  if (output) {
    variant.Render.Output = std::move(*output);
  }
  variant.Curve.Model = BrdfModel::GGX;
  variant.Curve.AnglesDegrees = {0.0F, 45.0F, 90.0F};
  variant.Curve.Response = {0.2F, 0.8F, 0.3F};
  variant.Curve.Measurements = {.Peak = 0.8F, .Mean = 0.43333334F, .Integral = 0.7F};
  variant.SceneLuminance = {.Mean = 0.4F, .Peak = 1.25F};
  session.Variants.push_back(std::move(variant));
  return session;
}

auto ReadArchiveBytes(const std::filesystem::path& path) -> std::vector<uint8_t> {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return {};
  }
  const auto size = input.tellg();
  if (size < 0) {
    return {};
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return input ? bytes : std::vector<uint8_t>{};
}

void WriteArchiveBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

TEST(AnalyticalFrameArchiveTests, SavesAndReopensDetachedCapturedBuffersAndSettings) {
  TemporaryArchiveDirectory directory;
  const auto path = directory.File("frozen-frame.aframe");
  const auto original = MakeSession();

  const Engine::Status save = AnalyticalFrame::AnalyticalFrameArchive::Save(path, original);
  ASSERT_TRUE(save) << (save ? "" : save.error().Message);
  const auto loaded = AnalyticalFrame::AnalyticalFrameArchive::Load(path);

  ASSERT_TRUE(loaded.has_value()) << (loaded ? "" : loaded.error().Message);
  EXPECT_EQ(loaded->TargetLabel, original.TargetLabel);
  EXPECT_EQ(
      loaded->Snapshot.Scene.entities.at(0).material.value_or(Engine::SceneMaterialDocument{}).baseColorTextureGuid,
      original.Snapshot.Scene.entities.at(0).material.value_or(Engine::SceneMaterialDocument{}).baseColorTextureGuid);
  EXPECT_EQ(loaded->Snapshot.SourceMaterial.baseColorTextureGuid,
            original.Snapshot.SourceMaterial.baseColorTextureGuid);
  EXPECT_FLOAT_EQ(loaded->Snapshot.SourceMaterialProperties.TextureTransforms[0].Offset.x, 0.25F);
  ASSERT_TRUE(loaded->Snapshot.FrameOverrides.OutputResolution.has_value());
  EXPECT_EQ(loaded->Snapshot.FrameOverrides.OutputResolution->Width, 2U);
  EXPECT_FLOAT_EQ(loaded->Snapshot.FrameOverrides.Roughness.value_or(-1.0F), 0.25F);
  EXPECT_TRUE(loaded->PostProcessCallbackPresent);
  EXPECT_FALSE(static_cast<bool>(loaded->Snapshot.PostProcess));
  EXPECT_EQ(loaded->ViewportState, original.ViewportState);
  ASSERT_EQ(loaded->Variants.size(), 1U);
  const auto& variant = loaded->Variants.front();
  EXPECT_EQ(variant.Render.Request.Model, Engine::Renderer::BrdfModel::GGX);
  EXPECT_EQ(variant.Render.Output.Width, 2U);
  EXPECT_EQ(variant.Render.Output.Height, 1U);
  EXPECT_FLOAT_EQ(variant.Render.Output.GetPixels(Engine::RenderAov::WorldNormal)[3],
                  original.Variants.front().Render.Output.GetPixels(Engine::RenderAov::WorldNormal)[3]);
  EXPECT_FLOAT_EQ(variant.Curve.Measurements.Integral, 0.7F);
  EXPECT_FLOAT_EQ(variant.SceneLuminance.Peak, 1.25F);
}

TEST(AnalyticalFrameArchiveTests, RejectsTruncatedAndCorruptArchivesWithoutReturningASession) {
  TemporaryArchiveDirectory directory;
  const auto path = directory.File("broken.aframe");
  ASSERT_TRUE(AnalyticalFrame::AnalyticalFrameArchive::Save(path, MakeSession()));
  auto bytes = ReadArchiveBytes(path);
  ASSERT_GT(bytes.size(), 24U);
  bytes.pop_back();
  WriteArchiveBytes(path, bytes);
  const auto loaded = AnalyticalFrame::AnalyticalFrameArchive::Load(path);
  EXPECT_FALSE(loaded.has_value());
  EXPECT_NE(loaded.error().Message.find("payload length"), std::string::npos);

  ASSERT_TRUE(AnalyticalFrame::AnalyticalFrameArchive::Save(path, MakeSession()));
  bytes = ReadArchiveBytes(path);
  ASSERT_GT(bytes.size(), 24U);
  bytes.back() ^= 0x5AU;
  WriteArchiveBytes(path, bytes);
  const auto corrupt = AnalyticalFrame::AnalyticalFrameArchive::Load(path);
  EXPECT_FALSE(corrupt.has_value());
  EXPECT_NE(corrupt.error().Message.find("checksum"), std::string::npos);
}

TEST(AnalyticalFrameArchiveTests, RejectsUnsupportedManifestVersion) {
  TemporaryArchiveDirectory directory;
  const auto path = directory.File("future-version.aframe");
  ASSERT_TRUE(AnalyticalFrame::AnalyticalFrameArchive::Save(path, MakeSession()));
  auto bytes = ReadArchiveBytes(path);
  ASSERT_GT(bytes.size(), 24U);
  uint64_t manifestSize = 0;
  for (uint32_t index = 0; index < 8; ++index) {
    manifestSize |= static_cast<uint64_t>(bytes[8U + index]) << (index * 8U);
  }
  ASSERT_LE(16U + manifestSize, bytes.size());
  auto manifest =
      nlohmann::json::parse(bytes.begin() + 16, bytes.begin() + static_cast<std::ptrdiff_t>(16U + manifestSize));
  manifest["formatVersion"] = 999;
  const std::string text = manifest.dump();
  std::vector<uint8_t> changed(bytes.begin(), bytes.begin() + 16);
  const uint64_t newSize = text.size();
  for (uint32_t index = 0; index < 8; ++index) {
    changed[8U + index] = static_cast<uint8_t>((newSize >> (index * 8U)) & 0xFFU);
  }
  changed.insert(changed.end(), text.begin(), text.end());
  changed.insert(changed.end(), bytes.begin() + static_cast<std::ptrdiff_t>(16U + manifestSize), bytes.end());
  WriteArchiveBytes(path, changed);

  const auto loaded = AnalyticalFrame::AnalyticalFrameArchive::Load(path);
  ASSERT_FALSE(loaded.has_value());
  EXPECT_NE(loaded.error().Message.find("Unsupported Analytical Frame archive version"), std::string::npos);
}

TEST(AnalyticalFrameArchiveTests, RejectsInvalidViewportStateBeforeWriting) {
  TemporaryArchiveDirectory directory;
  auto session = MakeSession();
  session.ViewportState["PinnedPixel"] = {99U, 0U};
  const Engine::Status saved = AnalyticalFrame::AnalyticalFrameArchive::Save(directory.File("invalid.aframe"), session);
  ASSERT_FALSE(saved.has_value());
  EXPECT_NE(saved.error().Message.find("outside the captured image"), std::string::npos);

  session = MakeSession();
  session.ViewportState["SelectedAov"] = "depth";
  session.ViewportState["Mode"] = "RelativeLuminanceError";
  const Engine::Status incompatible =
      AnalyticalFrame::AnalyticalFrameArchive::Save(directory.File("incompatible-error-mode.aframe"), session);
  ASSERT_FALSE(incompatible.has_value());
  EXPECT_NE(incompatible.error().Message.find("requires the final-color AOV"), std::string::npos);
}

TEST(AnalyticalFrameArchiveTests, RejectsMutatedOutputPlanesBeforeWriting) {
  TemporaryArchiveDirectory directory;

  auto wrongOrder = MakeSession();
  wrongOrder.Variants.front().Render.Output.GetMutablePlanes().front().Aov = Engine::RenderAov::Depth;
  const Engine::Status orderSave =
      AnalyticalFrame::AnalyticalFrameArchive::Save(directory.File("wrong-order.aframe"), wrongOrder);
  ASSERT_FALSE(orderSave.has_value());
  EXPECT_NE(orderSave.error().Message.find("AOV, channel layout, or sample count"), std::string::npos);

  auto wrongChannels = MakeSession();
  wrongChannels.Variants.front().Render.Output.GetMutablePlanes().front().ChannelCount = 3;
  const Engine::Status channelSave =
      AnalyticalFrame::AnalyticalFrameArchive::Save(directory.File("wrong-channels.aframe"), wrongChannels);
  ASSERT_FALSE(channelSave.has_value());
  EXPECT_NE(channelSave.error().Message.find("AOV, channel layout, or sample count"), std::string::npos);

  auto wrongSamples = MakeSession();
  wrongSamples.Variants.front().Render.Output.GetMutablePlanes().front().Pixels.pop_back();
  const Engine::Status sampleSave =
      AnalyticalFrame::AnalyticalFrameArchive::Save(directory.File("wrong-samples.aframe"), wrongSamples);
  ASSERT_FALSE(sampleSave.has_value());
  EXPECT_NE(sampleSave.error().Message.find("AOV, channel layout, or sample count"), std::string::npos);
}

} // namespace
