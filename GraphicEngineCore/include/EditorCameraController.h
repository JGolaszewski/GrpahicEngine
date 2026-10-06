#pragma once

#include "CoreExport.h"

#include <cstdint>

namespace Renderer
{
class Camera;
}

namespace Engine
{

// Translates editor navigation input into camera movement and orientation.
// Camera math and controller state are kept outside the Editor UI class.
class ENGINE_API EditorCameraController
{
  public:
    struct InputState
    {
        bool MoveForward = false;
        bool MoveBackward = false;
        bool MoveRight = false;
        bool MoveLeft = false;
        bool MoveUp = false;
        bool MoveDown = false;
        bool RightMouseDown = false;
        bool ViewportHovered = false;
        bool TextInputActive = false;
    };

    void SetCamera(Renderer::Camera *camera) noexcept;
    void SetInput(const InputState &input) noexcept;
    void AddMouseDelta(float deltaX, float deltaY) noexcept;
    bool Zoom(float wheelDelta) noexcept;
    void Update(float deltaTime) noexcept;

    bool ShouldCaptureCursor() const noexcept;

  private:
    Renderer::Camera *m_Camera = nullptr;
    InputState m_Input;
    float m_MouseDeltaX = 0.0f;
    float m_MouseDeltaY = 0.0f;
    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
};
} // namespace Engine
