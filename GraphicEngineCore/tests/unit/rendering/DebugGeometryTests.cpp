#include "rendering/include/renderer/DebugGeometry.h"

#include "rendering/include/renderer/Light.h"
#include "scene/include/Scene.h"

#include <gtest/gtest.h>

#include <memory>

namespace
{
TEST(DebugGeometryTests, StoresLinesTrianglesAndOnlyValidLightTargets)
{
    Engine::Renderer::DebugGeometry geometry;
    EXPECT_TRUE(geometry.Empty());

    const DirectX::XMFLOAT4 color(0.2f, 0.4f, 0.6f, 1.0f);
    geometry.AddLine({1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, color);
    geometry.AddTriangle({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, color);
    geometry.AddLightTarget({Engine::InvalidLightId, {}, {}, 1.0f, DirectX::XM_PIDIV4});

    EXPECT_FALSE(geometry.Empty());
    ASSERT_EQ(geometry.GetLineVertices().size(), 2U);
    EXPECT_EQ(geometry.GetLineVertices().front().Position.x, 1.0f);
    EXPECT_EQ(geometry.GetLineVertices().back().Position.z, 6.0f);
    EXPECT_EQ(geometry.GetLineVertices().front().Color.w, 1.0f);
    ASSERT_EQ(geometry.GetTriangleVertices().size(), 3U);
    EXPECT_EQ(geometry.GetTriangleVertices()[1].Position.x, 1.0f);
    EXPECT_TRUE(geometry.GetLightTargets().empty());

    geometry.AddLightTarget({1, {1.0f, 2.0f, 3.0f}, {0.0f, -1.0f, 0.0f}, 5.0f, DirectX::XM_PIDIV4});
    ASSERT_EQ(geometry.GetLightTargets().size(), 1U);
    EXPECT_EQ(geometry.GetLightTargets().front().Id, 1U);

    geometry.Clear();
    EXPECT_TRUE(geometry.Empty());
    EXPECT_TRUE(geometry.GetLightTargets().empty());
}

TEST(DebugGeometryTests, BuildsConstructionPlaneWithoutLightGizmos)
{
    Engine::Scene scene;
    Engine::Renderer::DebugRenderSettings settings;
    settings.ShowWireframePlane = true;
    Engine::Renderer::DebugGeometry geometry;
    geometry.AddLine({99.0f, 99.0f, 99.0f}, {}, {1.0f, 1.0f, 1.0f, 1.0f});

    Engine::Renderer::DebugGeometryBuilder::BuildScene(scene, settings, geometry);

    EXPECT_EQ(geometry.GetLineVertices().size(), 84U);
    EXPECT_TRUE(geometry.GetTriangleVertices().empty());
    EXPECT_TRUE(geometry.GetLightTargets().empty());
}

TEST(DebugGeometryTests, BuildsSelectedAndUnselectedSpotlightGizmos)
{
    Engine::Scene scene;
    auto verticalLight = std::make_shared<Engine::Renderer::SpotLight>();
    verticalLight->SetPosition({1.0f, 2.0f, 3.0f});
    verticalLight->SetDirection({0.0f, 1.0f, 0.0f});
    verticalLight->SetRange(2000.0f);
    verticalLight->SetConeAngles(35.0f, 70.0f);
    const Engine::LightId selectedId = scene.AddLight(verticalLight);

    auto defaultLight = std::make_shared<Engine::Renderer::SpotLight>();
    defaultLight->SetRange(0.0f);
    const Engine::LightId otherId = scene.AddLight(defaultLight);

    Engine::Renderer::DebugRenderSettings settings;
    settings.ShowLightGizmos = true;
    settings.SelectedLightId = selectedId;
    Engine::Renderer::DebugGeometry geometry;

    Engine::Renderer::DebugGeometryBuilder::BuildScene(scene, settings, geometry);

    ASSERT_EQ(geometry.GetLightTargets().size(), 2U);
    EXPECT_EQ(geometry.GetLightTargets()[0].Id, selectedId);
    EXPECT_EQ(geometry.GetLightTargets()[0].Range, 1000.0f);
    EXPECT_EQ(geometry.GetLightTargets()[1].Id, otherId);
    ASSERT_EQ(geometry.GetLineVertices().size(), 268U);
    EXPECT_EQ(geometry.GetLineVertices()[0].Color.x, 1.0f);
    EXPECT_EQ(geometry.GetLineVertices()[0].Color.y, 1.0f);
    EXPECT_EQ(geometry.GetLineVertices()[134].Color.x, defaultLight->GetColor().x);
}
} // namespace
