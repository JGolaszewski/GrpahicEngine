#include "rendering/include/renderer/Camera.h"
#include "runtime/include/EditorCameraController.h"

#include <gtest/gtest.h>

#include <limits>

namespace
{

TEST(EditorCameraControllerTests, CursorCaptureRequiresRightMouseAndHoveredViewport)
{
    Engine::EditorCameraController controller;
    Engine::EditorCameraController::InputState input;
    input.RightMouseDown = true;
    controller.SetInput(input);
    EXPECT_FALSE(controller.ShouldCaptureCursor());

    input.ViewportHovered = true;
    controller.SetInput(input);
    EXPECT_TRUE(controller.ShouldCaptureCursor());

    input.RightMouseDown = false;
    controller.SetInput(input);
    EXPECT_FALSE(controller.ShouldCaptureCursor());
}

TEST(EditorCameraControllerTests, ZoomRejectsMissingCameraAndNonFiniteInput)
{
    Engine::EditorCameraController controller;
    EXPECT_FALSE(controller.Zoom(1.0f));

    Engine::Renderer::Camera camera(DirectX::XM_PIDIV4, 0.1f, 100.0f);
    controller.SetCamera(&camera);
    EXPECT_FALSE(controller.Zoom(std::numeric_limits<float>::quiet_NaN()));
}

TEST(EditorCameraControllerTests, ZoomClampsProjectionToSupportedFieldOfView)
{
    Engine::Renderer::Camera camera(DirectX::XM_PIDIV4, 0.1f, 100.0f);
    Engine::EditorCameraController controller;
    controller.SetCamera(&camera);

    ASSERT_TRUE(controller.Zoom(1000.0f));
    EXPECT_NEAR(camera.GetFovRadians(), DirectX::XMConvertToRadians(20.0f), 1e-5f);
    ASSERT_TRUE(controller.Zoom(-1000.0f));
    EXPECT_NEAR(camera.GetFovRadians(), DirectX::XMConvertToRadians(90.0f), 1e-5f);
}

TEST(EditorCameraControllerTests, MovementRequiresHoveredViewportWithoutTextInput)
{
    Engine::Renderer::Camera camera(DirectX::XM_PIDIV4, 0.1f, 100.0f);
    Engine::EditorCameraController controller;
    controller.SetCamera(&camera);
    controller.AddMouseDelta(12.0f, -8.0f);
    Engine::EditorCameraController::InputState input;
    input.MoveForward = true;
    controller.SetInput(input);
    controller.Update(1.0f);
    EXPECT_FLOAT_EQ(camera.m_Transform.Position.z, 0.0f);

    input.ViewportHovered = true;
    input.TextInputActive = true;
    controller.SetInput(input);
    controller.Update(1.0f);
    EXPECT_FLOAT_EQ(camera.m_Transform.Position.z, 0.0f);
}

TEST(EditorCameraControllerTests, MovesCameraAlongLocalAxesAndRotatesWhileDragging)
{
    Engine::Renderer::Camera camera(DirectX::XM_PIDIV4, 0.1f, 100.0f);
    Engine::EditorCameraController controller;
    controller.SetCamera(&camera);
    Engine::EditorCameraController::InputState input;
    input.ViewportHovered = true;
    input.RightMouseDown = true;
    input.MoveForward = true;
    input.MoveRight = true;
    input.MoveUp = true;
    controller.SetInput(input);
    controller.AddMouseDelta(50.0f, 100.0f);

    controller.Update(0.5f);

    EXPECT_GT(camera.m_Transform.Position.x, 0.0f);
    EXPECT_FLOAT_EQ(camera.m_Transform.Position.y, 4.0f);
    EXPECT_GT(camera.m_Transform.Position.z, 0.0f);
}

} // namespace
