#include "rendering/include/renderer/Camera.h"
#include "rendering/include/renderer/Light.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{

TEST(CameraValueTests, RejectsInvalidProjectionAndKeepsConstructorFallback)
{
    Engine::Renderer::Camera camera(std::numeric_limits<float>::quiet_NaN(), 0.1f, 100.0f);
    EXPECT_FLOAT_EQ(camera.GetFovRadians(), DirectX::XM_PIDIV4);
    EXPECT_FLOAT_EQ(camera.GetNearPlane(), 0.1f);
    EXPECT_FLOAT_EQ(camera.GetFarPlane(), 100.0f);

    EXPECT_FALSE(camera.SetProjection(std::numeric_limits<float>::infinity(), 0.1f, 100.0f));
    EXPECT_FALSE(camera.SetProjection(0.0f, 0.1f, 100.0f));
    EXPECT_FALSE(camera.SetProjection(DirectX::XM_PI, 0.1f, 100.0f));
    EXPECT_FALSE(camera.SetProjection(DirectX::XM_PIDIV4, 0.0f, 100.0f));
    EXPECT_FALSE(camera.SetProjection(DirectX::XM_PIDIV4, 10.0f, 1.0f));
}

TEST(CameraValueTests, BuildsFiniteViewProjectionAndClampsNonpositiveAspect)
{
    Engine::Renderer::Camera camera(DirectX::XM_PIDIV4, 0.1f, 100.0f);
    Engine::Components::Transform transform;
    transform.SetPosition(2.0f, 3.0f, 4.0f);
    camera.SetPosition(transform);

    const auto projection = camera.GetProjection(-1.0f);
    const auto viewProjection = camera.GetViewProjection(16.0f / 9.0f);
    DirectX::XMFLOAT4X4 projectionValues;
    DirectX::XMFLOAT4X4 viewProjectionValues;
    DirectX::XMStoreFloat4x4(&projectionValues, projection);
    DirectX::XMStoreFloat4x4(&viewProjectionValues, viewProjection);

    EXPECT_TRUE(std::isfinite(projectionValues._11));
    EXPECT_TRUE(std::isfinite(projectionValues._22));
    EXPECT_TRUE(std::isfinite(viewProjectionValues._11));
    EXPECT_TRUE(std::isfinite(viewProjectionValues._44));
}

TEST(SpotLightTests, ClampsValuesAndProducesShaderCompatibleLightData)
{
    Engine::Renderer::SpotLight light({0.0f, -1.0f, 0.0f}, {0.25f, 0.5f, 0.75f}, -4.0f);
    light.SetPosition({1.0f, 2.0f, 3.0f});
    light.SetUsePosition(true);
    light.SetIntensity(2.5f);
    light.SetRange(0.0f);
    light.SetConeAngles(70.0f, 20.0f);
    light.SetCastShadows(false);
    light.SetShadowBias(-1.0f);
    light.SetShadowNormalBias(0.02f);
    light.LookAt({1.0f, 2.0f, 13.0f});

    EXPECT_FLOAT_EQ(light.GetIntensity(), 2.5f);
    EXPECT_FLOAT_EQ(light.GetRange(), 0.001f);
    EXPECT_FLOAT_EQ(light.GetInnerAngleDegrees(), 20.0f);
    EXPECT_FLOAT_EQ(light.GetOuterAngleDegrees(), 70.0f);
    EXPECT_FLOAT_EQ(light.GetShadowBias(), 0.0f);
    EXPECT_FLOAT_EQ(light.GetShadowNormalBias(), 0.02f);
    EXPECT_TRUE(light.GetUsePosition());
    EXPECT_FALSE(light.GetCastShadows());

    const auto data = light.GetDirectionalLightData();
    EXPECT_FLOAT_EQ(data.DirectionIntensity.w, 2.5f);
    EXPECT_FLOAT_EQ(data.ColorUsePosition.x, 0.25f);
    EXPECT_FLOAT_EQ(data.ColorUsePosition.w, 1.0f);
    EXPECT_FLOAT_EQ(data.PositionRange.z, 3.0f);
    EXPECT_FLOAT_EQ(data.PositionRange.w, 0.001f);
    EXPECT_FLOAT_EQ(data.ShadowParams.y, 0.0f);
    EXPECT_FLOAT_EQ(data.ShadowParams.w, 0.0f);
}

TEST(SpotLightTests, LeavesDirectionUnchangedForDegenerateLookAt)
{
    Engine::Renderer::SpotLight light;
    light.SetDirection({1.0f, 0.0f, 0.0f});
    light.LookAt(light.GetPosition());

    const auto direction = light.GetDirection();
    EXPECT_FLOAT_EQ(direction.x, 1.0f);
    EXPECT_FLOAT_EQ(direction.y, 0.0f);
    EXPECT_FLOAT_EQ(direction.z, 0.0f);
}

} // namespace
