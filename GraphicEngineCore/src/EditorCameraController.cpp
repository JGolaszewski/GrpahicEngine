#include "EditorCameraController.h"

#include "renderer/Camera.h"

#include <algorithm>
#include <cmath>

namespace Engine {

    void EditorCameraController::SetCamera(Renderer::Camera* camera) noexcept {
        if (m_Camera != camera) {
            m_Camera = camera;
        }
    }

    void EditorCameraController::SetInput(const InputState& input) noexcept {
        m_Input = input;
    }

    void EditorCameraController::AddMouseDelta(float deltaX, float deltaY) noexcept {
        m_MouseDeltaX += deltaX;
        m_MouseDeltaY += deltaY;
    }

    bool EditorCameraController::Zoom(float wheelDelta) noexcept {
        if (!m_Camera || !std::isfinite(wheelDelta)) {
            return false;
        }

        constexpr float zoomSensitivity = 0.05f;
        const float minFov = DirectX::XMConvertToRadians(20.0f);
        const float maxFov = DirectX::XMConvertToRadians(90.0f);
        const float fov = std::clamp(
            m_Camera->GetFovRadians() - wheelDelta * zoomSensitivity,
            minFov,
            maxFov);

        return m_Camera->SetProjection(fov, m_Camera->GetNearPlane(), m_Camera->GetFarPlane());
    }

    void EditorCameraController::Update(float deltaTime) noexcept {
        if (!m_Camera || !m_Input.ViewportHovered || m_Input.TextInputActive) {
            m_MouseDeltaX = 0.0f;
            m_MouseDeltaY = 0.0f;
            return;
        }

        constexpr float moveSpeed = 8.0f;
        constexpr float lookSensitivity = 0.0035f;
        constexpr float maxPitch = 1.5f;

        float moveForward = 0.0f;
        float moveRight = 0.0f;
        float moveUp = 0.0f;

        if (m_Input.MoveForward) moveForward += 1.0f;
        if (m_Input.MoveBackward) moveForward -= 1.0f;
        if (m_Input.MoveRight) moveRight += 1.0f;
        if (m_Input.MoveLeft) moveRight -= 1.0f;
        if (m_Input.MoveUp) moveUp += 1.0f;
        if (m_Input.MoveDown) moveUp -= 1.0f;

        const float step = moveSpeed * deltaTime;
        const float sinYaw = std::sin(m_Yaw);
        const float cosYaw = std::cos(m_Yaw);

        auto& position = m_Camera->m_Transform.Position;
        position.x += (sinYaw * moveForward + cosYaw * moveRight) * step;
        position.z += (cosYaw * moveForward - sinYaw * moveRight) * step;
        position.y += moveUp * step;

        if (m_Input.RightMouseDown) {
            m_Yaw += m_MouseDeltaX * lookSensitivity;
            m_Pitch = std::clamp(m_Pitch + m_MouseDeltaY * lookSensitivity, -maxPitch, maxPitch);
            m_Camera->m_Transform.SetRotation(m_Pitch, m_Yaw, 0.0f);
        }

        m_MouseDeltaX = 0.0f;
        m_MouseDeltaY = 0.0f;
    }

    bool EditorCameraController::ShouldCaptureCursor() const noexcept {
        return m_Input.RightMouseDown && m_Input.ViewportHovered;
    }
}
