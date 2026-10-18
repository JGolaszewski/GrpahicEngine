#include "panels/include/TransformRotation.h"

#include <gtest/gtest.h>

#include <DirectXMath.h>

#include <array>
#include <cmath>

namespace {

[[nodiscard]] auto NearlySameOrientation(const std::array<float, 4>& left, const std::array<float, 4>& right) -> bool {
  const float dot = left[0] * right[0] + left[1] * right[1] + left[2] * right[2] + left[3] * right[3];
  return std::abs(std::abs(dot) - 1.0F) <= 1.0e-5F;
}

TEST(InspectorTransformRotationTests, DegreesMapPositiveNegativeAndCombinedAnglesToDirectXOrientation) {
  constexpr std::array cases = {std::array<float, 3>{30.0F, 0.0F, 0.0F}, std::array<float, 3>{0.0F, -45.0F, 0.0F},
                                std::array<float, 3>{-20.0F, 35.0F, -15.0F}};
  for (const auto& degrees : cases) {
    DirectX::XMFLOAT4 expected{};
    DirectX::XMStoreFloat4(&expected,
                           DirectX::XMQuaternionRotationRollPitchYaw(DirectX::XMConvertToRadians(degrees[0]),
                                                                     DirectX::XMConvertToRadians(degrees[1]),
                                                                     DirectX::XMConvertToRadians(degrees[2])));
    const std::array<float, 4> actual = Editor::TransformRotation::FromPitchYawRollDegrees(degrees);
    EXPECT_TRUE(NearlySameOrientation(actual, {expected.x, expected.y, expected.z, expected.w}));
  }
}

TEST(InspectorTransformRotationTests, QuaternionInspectorRoundTripPreservesOrientation) {
  constexpr std::array cases = {std::array<float, 3>{30.0F, 0.0F, 0.0F}, std::array<float, 3>{0.0F, -45.0F, 0.0F},
                                std::array<float, 3>{-20.0F, 35.0F, -15.0F}};
  for (const auto& degrees : cases) {
    const auto original = Editor::TransformRotation::FromPitchYawRollDegrees(degrees);
    const auto restored =
        Editor::TransformRotation::FromPitchYawRollDegrees(Editor::TransformRotation::ToPitchYawRollDegrees(original));
    EXPECT_TRUE(NearlySameOrientation(original, restored));
  }
}

} // namespace
