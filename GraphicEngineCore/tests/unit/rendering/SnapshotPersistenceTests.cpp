#include "rendering/include/persistence/SnapshotPersistence.h"

#include "runtime/include/Result.h"

#include <gtest/gtest.h>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4702)
#endif
#include <OpenImageIO/imageio.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace {
using namespace Engine;
using namespace Engine::SnapshotPersistence;

class TempDirectory final {
public:
  TempDirectory() {
    m_Path = std::filesystem::temp_directory_path() /
             ("GraphicEngineSnapshotPersistence-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(m_Path);
  }
  ~TempDirectory() noexcept {
    std::error_code ignored;
    std::filesystem::remove_all(m_Path, ignored);
  }
  auto Path() const -> const std::filesystem::path& { return m_Path; }

private:
  std::filesystem::path m_Path;
};

auto MakeFrame() -> Result<SnapshotFrame> {
  SnapshotFrameMetadata metadata;
  metadata.CaptureTimeUtc = "2026-10-19T12:00:00Z";
  metadata.SceneIdentity = "scene-identity";
  metadata.Camera.Identity = "camera-identity";
  metadata.Camera.Position = {1.0F, -2.0F, 3.5F};
  metadata.Camera.Rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  metadata.Camera.AuthoredWidth = 2;
  metadata.Camera.AuthoredHeight = 1;
  metadata.OutputWidth = 2;
  metadata.OutputHeight = 1;
  metadata.RenderSettings.ShadowsEnabled = false;
  metadata.Color.LuminanceUnit = SnapshotLuminanceUnit::Nits;
  metadata.Color.NitsPerSceneUnit = 80.0F;
  std::vector<RenderOutputPlane> planes;
  for (RenderAov aov : {RenderAov::FinalColor, RenderAov::Depth, RenderAov::WorldNormal, RenderAov::SpecularLighting,
                        RenderAov::DiffuseLighting}) {
    const auto channels = RenderOutput::GetDescriptor(aov).ChannelCount;
    std::vector<float> values(static_cast<size_t>(channels) * 2U);
    for (size_t i = 0; i < values.size(); ++i)
      values[i] = static_cast<float>(i + static_cast<size_t>(aov) * 10U) / 7.0F;
    planes.push_back({aov, channels, std::move(values)});
  }
  SnapshotMaterialManifestEntry entry;
  entry.Id = 0xF0000001U;
  entry.EntityIdentity = "entity-identity";
  entry.SubmeshIndex = 4;
  entry.MaterialSlot = 2;
  entry.MaterialIdentity = "material-identity";
  entry.CapturedValues.metallic = 0.75F;
  entry.CapturedValues.baseColorTextureGuid = "texture-identity";
  entry.ResourceIdentities = {"texture-identity"};
  return SnapshotFrame::Create(std::move(metadata), {std::move(entry)}, 2, 1, std::move(planes), {0U, 0xF0000001U},
                               {0U, 0xFFFFFFFFU});
}

TEST(SnapshotPersistenceTests, ExrRoundTripPreservesFloatUInt32AndMetadata) {
  TempDirectory temp;
  auto source = MakeFrame();
  ASSERT_TRUE(source);
  const auto path = temp.Path() / "capture.exr";
  ASSERT_TRUE(SnapshotExr::Write(*source, path));
  auto loaded = SnapshotExr::Read(path);
  ASSERT_TRUE(loaded) << loaded.error().Message;
  EXPECT_EQ(loaded->GetWidth(), source->GetWidth());
  EXPECT_EQ(loaded->GetHeight(), source->GetHeight());
  ASSERT_EQ(loaded->GetFloatPlanes().size(), 5U);
  for (size_t plane = 0; plane < 5; ++plane) {
    EXPECT_EQ(loaded->GetFloatPlanes()[plane].Aov, source->GetFloatPlanes()[plane].Aov);
    EXPECT_EQ(loaded->GetFloatPlanes()[plane].ChannelCount, source->GetFloatPlanes()[plane].ChannelCount);
    EXPECT_EQ(loaded->GetFloatPlanes()[plane].Pixels, source->GetFloatPlanes()[plane].Pixels);
  }
  EXPECT_TRUE(std::ranges::equal(loaded->GetMaterialInstanceIds(), source->GetMaterialInstanceIds()));
  EXPECT_TRUE(std::ranges::equal(loaded->GetOverdraw(), source->GetOverdraw()));
  EXPECT_EQ(loaded->GetMetadata().Camera.Position, source->GetMetadata().Camera.Position);
  EXPECT_EQ(loaded->GetMetadata().Color.NitsPerSceneUnit, source->GetMetadata().Color.NitsPerSceneUnit);
  ASSERT_EQ(loaded->GetManifest().size(), 1U);
  EXPECT_EQ(loaded->GetManifest()[0].CapturedValues, source->GetManifest()[0].CapturedValues);
  EXPECT_EQ(loaded->GetManifest()[0].ResourceIdentities, source->GetManifest()[0].ResourceIdentities);
}

TEST(SnapshotPersistenceTests, RejectsCorruptAndIncompleteExrPayloads) {
  TempDirectory temp;
  const auto corrupt = temp.Path() / "broken.exr";
  {
    std::ofstream output(corrupt, std::ios::binary);
    output << "not an exr";
  }
  EXPECT_FALSE(SnapshotExr::Read(corrupt));

  const auto incomplete = temp.Path() / "incomplete.exr";
  auto writer = OIIO::ImageOutput::create("openexr");
  ASSERT_TRUE(writer);
  OIIO::ImageSpec spec(1, 1, 4, OIIO::TypeDesc::FLOAT);
  spec.channelnames = {"R", "G", "B", "A"};
  spec.attribute("engine:plane", "final_color");
  ASSERT_TRUE(writer->open(incomplete.string(), spec));
  const std::array<float, 4> pixel{0.1F, 0.2F, 0.3F, 1.0F};
  ASSERT_TRUE(writer->write_image(OIIO::span<const float>(pixel.data(), pixel.size())));
  ASSERT_TRUE(writer->close());
  auto missing = SnapshotExr::Read(incomplete);
  EXPECT_FALSE(missing);
  EXPECT_NE(missing.error().Message.find("missing"), std::string::npos);
}

TEST(SnapshotPersistenceTests, WorkspaceRoundTripResolvesRelativeExrsAndViewState) {
  TempDirectory temp;
  auto frame = MakeFrame();
  ASSERT_TRUE(frame);
  const auto exr = temp.Path() / "captures" / "frame.exr";
  std::filesystem::create_directories(exr.parent_path());
  ASSERT_TRUE(SnapshotExr::Write(*frame, exr));

  SnapshotWorkspaceDocument document;
  SnapshotFrameViewState tab;
  tab.ExrPath = std::filesystem::path("captures") / "frame.exr";
  tab.SelectedAov = RenderAov::WorldNormal;
  tab.PinnedPixel = SnapshotPinnedPixelState{1, 0};
  tab.Roi = SnapshotRoiState{0, 0, 2, 1};
  tab.Zoom = 2.5F;
  tab.PanX = -12.0F;
  tab.PanY = 3.0F;
  document.Tabs.push_back(tab);
  document.Comparison = SnapshotComparisonState{0, 0, SnapshotComparisonMetric::RgbRms, 0.25F};
  // A comparison needs distinct tabs, so create a second frame reference.
  document.Tabs.push_back(tab);
  document.Comparison->CurrentTab = 1;
  document.SelectedTab = 1;
  const auto file = temp.Path() / "workspace.aframe";
  ASSERT_TRUE(SnapshotWorkspaceFile::Save(file, document));
  auto loaded = SnapshotWorkspaceFile::Load(file);
  ASSERT_TRUE(loaded) << loaded.error().Message;
  ASSERT_EQ(loaded->Frames.size(), 2U);
  ASSERT_EQ(loaded->ResolvedExrPaths.size(), 2U);
  EXPECT_EQ(loaded->ResolvedExrPaths[0], std::filesystem::absolute(exr));
  EXPECT_EQ(loaded->Document.Tabs[0].SelectedAov, RenderAov::WorldNormal);
  EXPECT_EQ(loaded->Document.Tabs[0].PinnedPixel, tab.PinnedPixel);
  EXPECT_EQ(loaded->Document.Tabs[0].Roi->Width, 2U);
  EXPECT_FLOAT_EQ(loaded->Document.Tabs[0].Zoom, 2.5F);
  EXPECT_FLOAT_EQ(loaded->Document.Tabs[0].PanX, -12.0F);
  ASSERT_TRUE(loaded->Document.Comparison);
  EXPECT_EQ(loaded->Document.Comparison->Metric, SnapshotComparisonMetric::RgbRms);
  EXPECT_FLOAT_EQ(loaded->Document.Comparison->WipePosition, 0.25F);
  EXPECT_EQ(loaded->Document.SelectedTab, 1U);
}

TEST(SnapshotPersistenceTests, UnsupportedVersionsAndMissingOrCorruptReferencesFailAtomically) {
  TempDirectory temp;
  auto frame = MakeFrame();
  ASSERT_TRUE(frame);
  const auto payload = temp.Path() / "capture.exr";
  ASSERT_TRUE(SnapshotExr::Write(*frame, payload));
  SnapshotWorkspaceDocument document;
  SnapshotFrameViewState tab;
  tab.ExrPath = "capture.exr";
  document.Tabs.push_back(tab);
  const auto file = temp.Path() / "workspace.aframe";
  ASSERT_TRUE(SnapshotWorkspaceFile::Save(file, document));
  std::filesystem::remove(payload);
  auto missing = SnapshotWorkspaceFile::Load(file);
  EXPECT_FALSE(missing);
  EXPECT_NE(missing.error().Message.find("cannot open EXR"), std::string::npos);

  {
    std::ofstream corrupt(payload, std::ios::binary);
    corrupt << "bad";
  }
  auto badPayload = SnapshotWorkspaceFile::Load(file);
  EXPECT_FALSE(badPayload);
  EXPECT_NE(badPayload.error().Message.find("EXR"), std::string::npos);

  nlohmann::json json;
  {
    std::ifstream input(file);
    input >> json;
  }
  json["version"] = 99;
  {
    std::ofstream output(file);
    output << json.dump(2);
  }
  auto unsupported = SnapshotWorkspaceFile::Load(file);
  EXPECT_FALSE(unsupported);
  EXPECT_NE(unsupported.error().Message.find("unsupported"), std::string::npos);
}

TEST(SnapshotPersistenceTests, WorkspaceSaveRejectsInvalidStateWithoutReplacingDestination) {
  TempDirectory temp;
  const auto file = temp.Path() / "workspace.aframe";
  {
    std::ofstream existing(file);
    existing << "sentinel";
  }
  SnapshotWorkspaceDocument invalid;
  invalid.SelectedTab = 7;
  const auto result = SnapshotWorkspaceFile::Save(file, invalid);
  EXPECT_FALSE(result);
  std::ifstream input(file);
  std::string content;
  input >> content;
  EXPECT_EQ(content, "sentinel");
}

TEST(SnapshotPersistenceTests, MalformedWorkspaceJsonReturnsPathAwareError) {
  TempDirectory temp;
  const auto file = temp.Path() / "malformed.aframe";
  {
    std::ofstream output(file, std::ios::binary);
    output << "{ definitely not json";
  }
  const auto loaded = SnapshotWorkspaceFile::Load(file);
  ASSERT_FALSE(loaded);
  EXPECT_NE(loaded.error().Message.find(file.generic_string()), std::string::npos);
}

TEST(SnapshotPersistenceTests, LargePinnedPixelIsCheckedAgainstPayloadDimensions) {
  TempDirectory temp;
  auto frame = MakeFrame();
  ASSERT_TRUE(frame);
  const auto payload = temp.Path() / "capture.exr";
  ASSERT_TRUE(SnapshotExr::Write(*frame, payload));

  SnapshotWorkspaceDocument document;
  SnapshotFrameViewState tab;
  tab.ExrPath = payload;
  tab.PinnedPixel = SnapshotPinnedPixelState{std::numeric_limits<uint32_t>::max(), 0};
  document.Tabs.push_back(tab);
  const auto workspace = temp.Path() / "workspace.aframe";
  ASSERT_TRUE(SnapshotWorkspaceFile::Save(workspace, document));
  const auto loaded = SnapshotWorkspaceFile::Load(workspace);
  ASSERT_FALSE(loaded);
  EXPECT_NE(loaded.error().Message.find("pinned pixel lies outside"), std::string::npos);
}

} // namespace
