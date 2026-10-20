#include "rendering/src/DirectionalShadowFit.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace {
using Engine::Renderer::DirectionalShadowCamera;
using Engine::Renderer::FitDirectionalShadow;

auto MaxMatrixDifference(const DirectX::XMFLOAT4X4& left, const DirectX::XMFLOAT4X4& right) -> float {
  const float* leftValues = &left._11;
  const float* rightValues = &right._11;
  float difference = 0.0F;
  for (size_t index = 0; index < 16; ++index) {
    difference = (std::max)(difference, std::abs(leftValues[index] - rightValues[index]));
  }
  return difference;
}

auto CameraAt(float x, float farPlane = 500.0F) -> DirectionalShadowCamera {
  DirectionalShadowCamera camera;
  camera.Position = {x, 0.0F, 0.0F};
  camera.Forward = {0.0F, 0.0F, 1.0F};
  camera.Right = {1.0F, 0.0F, 0.0F};
  camera.Up = {0.0F, 1.0F, 0.0F};
  camera.VerticalFovRadians = DirectX::XMConvertToRadians(60.0F);
  camera.AspectRatio = 16.0F / 9.0F;
  camera.NearPlane = 0.1F;
  camera.FarPlane = farPlane;
  return camera;
}
} // namespace

TEST(DirectionalShadowFitTests, ProjectionRemainsStableWithinOneShadowTexelOfCameraMotion) {
  const DirectionalShadowCamera initialCamera = CameraAt(0.0F);
  const auto initial = FitDirectionalShadow(std::span(&initialCamera, 1), {0.0F, 0.0F, 1.0F}, 2048, 80.0F, 100.0F);
  ASSERT_TRUE(initial.has_value());
  const DirectionalShadowCamera movedCamera = CameraAt(initial->TexelWorldSize * 0.2F);
  const auto moved = FitDirectionalShadow(std::span(&movedCamera, 1), {0.0F, 0.0F, 1.0F}, 2048, 80.0F, 100.0F);
  ASSERT_TRUE(moved.has_value());

  EXPECT_NEAR(MaxMatrixDifference(initial->ViewProjection, moved->ViewProjection), 0.0F, 1.0e-6F);
  EXPECT_NEAR(initial->CoverageWidth, moved->CoverageWidth, 1.0e-6F);
}

TEST(DirectionalShadowFitTests, CoverageDoesNotGrowForCameraFarPlanesBeyondConfiguredDirectionalDistance) {
  const DirectionalShadowCamera longCamera = CameraAt(0.0F, 1000.0F);
  const DirectionalShadowCamera clippedCamera = CameraAt(0.0F, 120.0F);
  const auto longFit = FitDirectionalShadow(std::span(&longCamera, 1), {0.0F, -1.0F, 0.0F}, 1024, 120.0F, 100.0F);
  const auto clippedFit = FitDirectionalShadow(std::span(&clippedCamera, 1), {0.0F, -1.0F, 0.0F}, 1024, 120.0F, 100.0F);
  ASSERT_TRUE(longFit.has_value());
  ASSERT_TRUE(clippedFit.has_value());

  EXPECT_NEAR(longFit->CoverageWidth, clippedFit->CoverageWidth, 1.0e-5F);
  EXPECT_NEAR(longFit->CoverageDepth, clippedFit->CoverageDepth, 1.0e-4F);
  EXPECT_NEAR(longFit->TexelWorldSize, clippedFit->TexelWorldSize, 1.0e-6F);
}

TEST(DirectionalShadowFitTests, RejectsInvalidInputsAndKeepsOnlyCameraReceiverFootprint) {
  const DirectionalShadowCamera camera = CameraAt(0.0F, 1000.0F);
  EXPECT_FALSE(FitDirectionalShadow({}, {0.0F, -1.0F, 0.0F}, 1024, 120.0F, 100.0F).has_value());
  EXPECT_FALSE(FitDirectionalShadow(std::span(&camera, 1), {0.0F, 0.0F, 0.0F}, 1024, 120.0F, 100.0F).has_value());
  EXPECT_FALSE(FitDirectionalShadow(std::span(&camera, 1), {0.0F, -1.0F, 0.0F}, 0, 120.0F, 100.0F).has_value());
  DirectionalShadowCamera invalidBasis = camera;
  invalidBasis.Forward = {0.0F, 0.0F, 0.0F};
  EXPECT_FALSE(
      FitDirectionalShadow(std::span(&invalidBasis, 1), {0.0F, -1.0F, 0.0F}, 1024, 120.0F, 100.0F).has_value());

  const auto fit = FitDirectionalShadow(std::span(&camera, 1), {0.0F, -1.0F, 0.0F}, 1024, 120.0F, 100.0F);
  ASSERT_TRUE(fit.has_value());
  // Geometry outside the camera receiver footprint is not a fit input and cannot expand its XY extent.
  EXPECT_LT(fit->CoverageWidth, 300.0F);
  EXPECT_GT(fit->CoverageDepth, 100.0F);
}
