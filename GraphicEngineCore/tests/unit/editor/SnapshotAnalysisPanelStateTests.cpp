#include "panels/include/SnapshotAnalysisPanelState.h"

#include <gtest/gtest.h>

#include <array>
#include <utility>

namespace {

auto CreateOverdrawOnlyFrame() -> Engine::Result<Engine::SnapshotFrame> {
  Engine::SnapshotFrameMetadata metadata;
  metadata.CaptureTimeUtc = "2026-10-19T12:00:00Z";
  metadata.SceneIdentity = "scene-guid";
  metadata.Camera.Identity = "camera-guid";
  metadata.Camera.AuthoredWidth = 1;
  metadata.Camera.AuthoredHeight = 1;
  metadata.OutputWidth = 1;
  metadata.OutputHeight = 1;
  return Engine::SnapshotFrame::Create(std::move(metadata), {}, 1, 1, {}, {}, {3U});
}

} // namespace

TEST(SnapshotAnalysisPanelStateTests, CapturePlaneMaskDefaultsToAllAndRejectsEmptySelection) {
  Engine::SnapshotPlaneMask selected = DefaultSnapshotCapturePlanes;
  EXPECT_TRUE(HasSnapshotCapturePlaneSelection(selected));
  constexpr std::array planes = {Engine::SnapshotPlane::FinalColor,      Engine::SnapshotPlane::Depth,
                                 Engine::SnapshotPlane::WorldNormal,     Engine::SnapshotPlane::SpecularLighting,
                                 Engine::SnapshotPlane::DiffuseLighting, Engine::SnapshotPlane::MaterialInstanceId,
                                 Engine::SnapshotPlane::Overdraw};
  for (Engine::SnapshotPlane plane : planes) {
    EXPECT_TRUE(IsSnapshotCapturePlaneEnabled(selected, plane));
    selected = SetSnapshotCapturePlaneEnabled(selected, plane, false);
  }
  EXPECT_EQ(selected, 0U);
  EXPECT_FALSE(HasSnapshotCapturePlaneSelection(selected));

  selected = SetSnapshotCapturePlaneEnabled(selected, Engine::SnapshotPlane::Overdraw, true);
  EXPECT_EQ(selected, Engine::SnapshotPlaneMaskBit(Engine::SnapshotPlane::Overdraw));
  EXPECT_FALSE(IsSnapshotCapturePlaneEnabled(selected, Engine::SnapshotPlane::FinalColor));
}

TEST(SnapshotAnalysisPanelStateTests, PlaneSelectorOnlyOffersPlanesPresentInPartialFrame) {
  auto frame = CreateOverdrawOnlyFrame();
  ASSERT_TRUE(frame);

  const auto available = GetAvailableSnapshotPlanes(*frame);
  ASSERT_EQ(available.size(), 1U);
  EXPECT_EQ(available.front(), Engine::SnapshotPlane::Overdraw);
  EXPECT_EQ(SelectAvailableSnapshotPlane(*frame, Engine::SnapshotPlane::FinalColor), Engine::SnapshotPlane::Overdraw);
  EXPECT_EQ(SelectAvailableSnapshotPlane(*frame, Engine::SnapshotPlane::Overdraw), Engine::SnapshotPlane::Overdraw);
  EXPECT_FALSE(frame->HasPlane(Engine::SnapshotPlane::FinalColor));
}

TEST(SnapshotAnalysisPanelStateTests, CameraDefaultPrefersSelectedThenActivePriorityThenStableHandle) {
  const std::array cameras = {
      SnapshotAnalysisCameraOption{.Entity = {8U, 1U}, .Label = "active", .IsActive = true, .Priority = 5},
      SnapshotAnalysisCameraOption{.Entity = {3U, 1U}, .Label = "first"},
      SnapshotAnalysisCameraOption{.Entity = {5U, 1U}, .Label = "selected", .IsCurrentSelection = true},
      SnapshotAnalysisCameraOption{.Entity = {9U, 1U}, .Label = "higher active", .IsActive = true, .Priority = 9}};

  EXPECT_EQ(SelectPreferredSnapshotCamera(cameras), Engine::EntityHandle(5U, 1U));
  std::array activeOnly = {cameras[0], cameras[1], cameras[3]};
  EXPECT_EQ(SelectPreferredSnapshotCamera(activeOnly), Engine::EntityHandle(9U, 1U));
  std::array noActive = {cameras[1], SnapshotAnalysisCameraOption{.Entity = {2U, 4U}, .Label = "lower handle"}};
  EXPECT_EQ(SelectPreferredSnapshotCamera(noActive), Engine::EntityHandle(2U, 4U));
  EXPECT_FALSE(SelectPreferredSnapshotCamera(std::span<const SnapshotAnalysisCameraOption>{}).has_value());
}
