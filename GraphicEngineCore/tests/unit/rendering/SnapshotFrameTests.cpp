#include "rendering/include/SnapshotFrame.h"

#include <gtest/gtest.h>

#include <array>
#include <utility>

namespace {

auto Metadata(uint32_t width = 2, uint32_t height = 1) -> Engine::SnapshotFrameMetadata {
  Engine::SnapshotFrameMetadata metadata;
  metadata.CaptureTimeUtc = "2026-10-19T12:00:00Z";
  metadata.SceneIdentity = "scene-guid";
  metadata.Camera.Identity = "camera-guid";
  metadata.OutputWidth = width;
  metadata.OutputHeight = height;
  metadata.Camera.AuthoredWidth = width;
  metadata.Camera.AuthoredHeight = height;
  return metadata;
}

auto Planes(uint32_t width = 2, uint32_t height = 1) -> std::vector<Engine::RenderOutputPlane> {
  constexpr std::array<Engine::RenderAov, 5> aovs = {
      Engine::RenderAov::FinalColor, Engine::RenderAov::Depth, Engine::RenderAov::WorldNormal,
      Engine::RenderAov::SpecularLighting, Engine::RenderAov::DiffuseLighting};
  std::vector<Engine::RenderOutputPlane> planes;
  for (Engine::RenderAov aov : aovs) {
    const uint32_t channels = Engine::RenderOutput::GetDescriptor(aov).ChannelCount;
    planes.push_back({aov, channels, std::vector<float>(static_cast<size_t>(width) * height * channels, 1.0F)});
  }
  return planes;
}

auto Manifest(uint32_t id = 0xF0000001U) -> std::vector<Engine::SnapshotMaterialManifestEntry> {
  Engine::SnapshotMaterialManifestEntry entry;
  entry.Id = id;
  entry.EntityIdentity = "entity-guid";
  entry.MaterialIdentity = "material-guid";
  return {std::move(entry)};
}

auto CreateFrame() -> Engine::Result<Engine::SnapshotFrame> {
  return Engine::SnapshotFrame::Create(Metadata(), Manifest(), 2, 1, Planes(), {0U, 0xF0000001U}, {0U, 4U});
}

TEST(SnapshotFrameTests, OwnsCompleteFloatAndExactIntegerPlanesAndMetadata) {
  auto result = CreateFrame();
  ASSERT_TRUE(result);
  const Engine::SnapshotFrame& frame = *result;

  EXPECT_EQ(frame.GetWidth(), 2U);
  EXPECT_EQ(frame.GetHeight(), 1U);
  ASSERT_EQ(frame.GetFloatPlanes().size(), 5U);
  EXPECT_EQ(frame.GetFloatPlanes().front().Aov, Engine::RenderAov::FinalColor);
  ASSERT_EQ(frame.GetMaterialInstanceIds().size(), 2U);
  EXPECT_EQ(frame.GetMaterialInstanceIds()[0], 0U);
  EXPECT_EQ(frame.GetMaterialInstanceIds()[1], 0xF0000001U);
  EXPECT_EQ(frame.GetOverdraw()[1], 4U);
  EXPECT_EQ(frame.GetMetadata().Camera.Identity, "camera-guid");
  ASSERT_EQ(frame.GetManifest().size(), 1U);
  EXPECT_EQ(frame.GetManifest()[0].Id, 0xF0000001U);
}

TEST(SnapshotFrameTests, AcceptsSelectedPlaneSubsetAndReportsAvailability) {
  auto planes = Planes();
  planes.erase(planes.begin());
  planes.erase(planes.begin() + 1, planes.end());
  auto result = Engine::SnapshotFrame::Create(Metadata(), {}, 2, 1, std::move(planes), {}, {0U, 4U});
  ASSERT_TRUE(result);
  EXPECT_EQ(result->GetWidth(), 2U);
  EXPECT_EQ(result->GetHeight(), 1U);
  EXPECT_TRUE(result->HasPlane(Engine::SnapshotPlane::Depth));
  EXPECT_TRUE(result->HasPlane(Engine::SnapshotPlane::Overdraw));
  EXPECT_FALSE(result->HasPlane(Engine::SnapshotPlane::FinalColor));
  EXPECT_FALSE(result->HasPlane(Engine::SnapshotPlane::MaterialInstanceId));
  EXPECT_TRUE(result->GetFloatPlanes().front().Aov == Engine::RenderAov::Depth);
  EXPECT_TRUE(result->GetMaterialInstanceIds().empty());
}

TEST(SnapshotFrameTests, RejectsEmptySelectionAndMismatchedSelectedIntegerExtent) {
  EXPECT_FALSE(Engine::SnapshotFrame::Create(Metadata(), {}, 2, 1, {}, {}, {}));
  EXPECT_FALSE(Engine::SnapshotFrame::Create(Metadata(), {}, 2, 1, Planes(), {}, {0U}));
}

TEST(SnapshotFrameTests, RejectsUnsortedFloatPlanes) {
  auto planes = Planes();
  std::swap(planes[0], planes[1]);
  EXPECT_FALSE(Engine::SnapshotFrame::Create(Metadata(), Manifest(), 2, 1, std::move(planes), {0U, 0U}, {0U, 0U}));
}

TEST(SnapshotFrameTests, RejectsDuplicateFloatPlanes) {
  auto planes = Planes();
  planes[1] = planes[0];
  EXPECT_FALSE(Engine::SnapshotFrame::Create(Metadata(), Manifest(), 2, 1, std::move(planes), {0U, 0U}, {0U, 0U}));
}

TEST(SnapshotFrameTests, RejectsDimensionMismatchAndInvalidPhotometricCalibration) {
  auto metadata = Metadata();
  metadata.OutputWidth = 3;
  EXPECT_FALSE(Engine::SnapshotFrame::Create(std::move(metadata), Manifest(), 2, 1, Planes(), {0U, 0U}, {0U, 0U}));

  metadata = Metadata();
  metadata.Color.LuminanceUnit = Engine::SnapshotLuminanceUnit::Nits;
  metadata.Color.NitsPerSceneUnit = -1.0F;
  EXPECT_FALSE(Engine::SnapshotFrame::Create(std::move(metadata), Manifest(), 2, 1, Planes(), {0U, 0U}, {0U, 0U}));
}

TEST(SnapshotFrameTests, RejectsZeroOrDuplicateManifestIdsAndUnmappedPixels) {
  auto invalidManifest = Manifest(0U);
  EXPECT_FALSE(
      Engine::SnapshotFrame::Create(Metadata(), std::move(invalidManifest), 2, 1, Planes(), {0U, 0U}, {0U, 0U}));

  auto duplicateManifest = Manifest();
  duplicateManifest.push_back(duplicateManifest.front());
  EXPECT_FALSE(
      Engine::SnapshotFrame::Create(Metadata(), std::move(duplicateManifest), 2, 1, Planes(), {0U, 0U}, {0U, 0U}));

  EXPECT_FALSE(Engine::SnapshotFrame::Create(Metadata(), Manifest(), 2, 1, Planes(), {0U, 7U}, {0U, 0U}));
}

TEST(SnapshotFrameTests, RejectsInvalidCameraTimestampAndSampleCount) {
  auto metadata = Metadata();
  metadata.CaptureTimeUtc = "not-a-time";
  EXPECT_FALSE(Engine::SnapshotFrame::Create(std::move(metadata), Manifest(), 2, 1, Planes(), {0U, 0U}, {0U, 0U}));

  metadata = Metadata();
  metadata.RenderSettings.SampleCount = 4;
  EXPECT_FALSE(Engine::SnapshotFrame::Create(std::move(metadata), Manifest(), 2, 1, Planes(), {0U, 0U}, {0U, 0U}));
}

TEST(SnapshotFrameTests, AllowsZeroShadowCastersButRequiresValidShadowMapResolution) {
  auto metadata = Metadata();
  metadata.RenderSettings.MaxShadowCasters = 0;
  EXPECT_TRUE(Engine::SnapshotFrame::Create(std::move(metadata), {}, 2, 1, Planes(), {0U, 0U}, {0U, 0U}));

  metadata = Metadata();
  metadata.RenderSettings.ShadowMapResolution = 0;
  EXPECT_FALSE(Engine::SnapshotFrame::Create(std::move(metadata), {}, 2, 1, Planes(), {0U, 0U}, {0U, 0U}));
}

} // namespace
