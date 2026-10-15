#pragma once

#include "runtime/include/CoreExport.h"

#include <cstdint>

namespace Engine::Renderer
{
class Camera;
}

namespace Engine
{

/// Translates editor navigation input into camera movement and orientation.
/// Camera math and controller state are kept outside the Editor UI class.
class ENGINE_API EditorCameraController
{
  public:
    /// Per-frame editor navigation input used to move and orient the camera.
    struct InputState
    {
        bool MoveForward = false;     ///< Move along the camera's forward axis.
        bool MoveBackward = false;    ///< Move opposite the camera's forward axis.
        bool MoveRight = false;       ///< Move along the camera's right axis.
        bool MoveLeft = false;        ///< Move opposite the camera's right axis.
        bool MoveUp = false;          ///< Move along the world up axis.
        bool MoveDown = false;        ///< Move opposite the world up axis.
        bool RightMouseDown = false;  ///< Enable mouse-look while held.
        bool ViewportHovered = false; ///< Indicates pointer focus is over the viewport.
        bool TextInputActive = false; ///< Suppresses camera navigation during text entry.
    };

    /// Selects the camera controlled by this instance.
    /// @param camera Camera to control, or null to disable camera updates.
    void SetCamera(::Engine::Renderer::Camera *camera) noexcept;

    /// Replaces the current navigation input state.
    /// @param input Input sampled for the current frame.
    void SetInput(const InputState &input) noexcept;

    /// Accumulates pointer motion until the next Update call.
    /// @param deltaX Horizontal pointer movement in pixels.
    /// @param deltaY Vertical pointer movement in pixels.
    void AddMouseDelta(float deltaX, float deltaY) noexcept;

    /// Adjusts the camera field of view while respecting editor zoom limits.
    /// @param wheelDelta Scroll-wheel movement; positive values zoom in.
    /// @return True when the camera projection accepts the updated field of view.
    bool Zoom(float wheelDelta) noexcept;

    /// Applies movement and mouse-look input to the camera for one frame.
    /// @param deltaTime Elapsed frame time in seconds.
    void Update(float deltaTime) noexcept;

    /// Reports whether the right mouse button should capture the cursor.
    /// @return True when the pointer is over the viewport and right mouse is held.
    bool ShouldCaptureCursor() const noexcept;

  private:
    ::Engine::Renderer::Camera *m_Camera = nullptr;
    InputState m_Input;
    float m_MouseDeltaX = 0.0f;
    float m_MouseDeltaY = 0.0f;
    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
};
} // namespace Engine
