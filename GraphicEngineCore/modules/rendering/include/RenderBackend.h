#pragma once

#include "rendering/include/ViewportTarget.h"
#include "runtime/include/CoreExport.h"
#include "runtime/include/Window.h"

#include <cstdint>

namespace Engine {
/// Backend-neutral diagnostics shared by render backends and engine tooling.
struct BackendDiagnostics {
  const char* BackendName = "unknown"; ///< Stable backend name for diagnostics.
  bool DeviceReady = false;            ///< Whether the graphics device is available.
  bool VSyncEnabled = false;           ///< Whether vertical synchronization is currently enabled.
  bool HasVSync = false;               ///< Whether the backend supports changing vertical synchronization.
  uint32_t Width = 0;                  ///< Current swap-chain framebuffer width in pixels.
  uint32_t Height = 0;                 ///< Current swap-chain framebuffer height in pixels.
  uint32_t AdapterVendorId = 0;        ///< Graphics adapter vendor identifier.
  uint32_t AdapterDeviceId = 0;        ///< Graphics adapter device identifier.
  uint64_t DeviceGeneration = 0;       ///< Incremented when the backend recreates its device.
  uint64_t ValidationWarningCount = 0; ///< Graphics validation warnings observed so far.
  uint64_t ValidationErrorCount = 0;   ///< Graphics validation errors observed so far.
  int64_t LastPresentResult = 0;       ///< Backend result code from the last present operation.
  int64_t LastBackendError = 0;        ///< Most recent backend-specific error code.
  int64_t DeviceRemovalReason = 0;     ///< Device-removal reason code when the device was lost.
  float LastPresentTimeMs = 0.0f;      ///< Duration of the most recent present operation.
  float LastGpuFrameTimeMs = 0.0f;     ///< Most recent available GPU frame duration.
};

/// Platform graphics interface used by the engine without exposing API-specific types.
class ENGINE_API IRenderBackend {
public:
  /// Releases backend resources.
  virtual ~IRenderBackend() = default;

  /// Creates the graphics device and swap chain for a window.
  /// @param window Window that owns the native presentation surface.
  /// @return True when the backend is ready to render.
  virtual bool Initialize(IWindow* window) = 0;

  /// Presents the completed frame to the window.
  virtual void Present() = 0;

  /// Resizes the swap-chain framebuffer.
  /// @param width New width in pixels.
  /// @param height New height in pixels.
  virtual void Resize(int width, int height) = 0;

  /// Returns the backend-owned device as an opaque handle for private integration code.
  /// @return The device handle, or null when the device is unavailable.
  virtual void* GetDevice() const = 0;

  /// Returns the default presentation framebuffer as an opaque handle.
  /// @return The framebuffer handle, or null when no swap chain is active.
  virtual void* GetDefaultFramebuffer() const = 0;

  /// Returns a backend-neutral snapshot of device and presentation diagnostics.
  /// @return Current backend diagnostics.
  virtual BackendDiagnostics GetDiagnostics() const noexcept { return {}; }
  /// Reports whether vertical synchronization can be controlled.
  /// @return True when SetVSyncEnabled is supported.
  virtual bool SupportsVSyncControl() const noexcept { return false; }
  /// Requests a vertical-synchronization state change.
  /// @param enabled True to enable vertical synchronization.
  /// @return True when the requested state was applied.
  virtual bool SetVSyncEnabled(bool enabled) noexcept {
    (void)enabled;
    return false;
  }
  /// Initializes backend UI integration for a window.
  /// @param window Window used by the UI platform backend.
  /// @return True when UI rendering is ready.
  virtual bool InitializeEditorUi(IWindow* window) {
    (void)window;
    return false;
  }
  /// Begins a backend UI frame after the UI platform has started a frame.
  virtual void BeginEditorUiFrame() {}
  /// Submits UI draw data for rendering.
  /// @param drawData Opaque draw-data pointer owned by the UI integration.
  virtual void RenderEditorUi(void* drawData) { (void)drawData; }
  /// Releases resources allocated by InitializeEditorUi.
  virtual void ShutdownEditorUi() {}
  /// Registers a render target's color texture for UI sampling.
  /// @param target Render target whose color image should be displayed.
  /// @return Opaque UI texture identifier, or null when registration is unsupported.
  void* RegisterViewportTexture(const ViewportTarget& target) const {
    return RegisterViewportTextureHandle(target.GetColorTextureHandle());
  }

  /// Begins a named graphics debugging event when supported by the backend.
  /// @param name Event label encoded as a null-terminated wide string.
  virtual void BeginGpuEvent(const wchar_t* name) { (void)name; }
  /// Ends the most recently begun graphics debugging event.
  virtual void EndGpuEvent() {}

protected:
  /// Backend hook that registers an opaque color texture with its UI renderer.
  /// @param textureHandle Backend-owned color texture handle.
  /// @return Opaque UI texture identifier, or null when unsupported.
  virtual void* RegisterViewportTextureHandle(void* textureHandle) const {
    (void)textureHandle;
    return nullptr;
  }
};
} // namespace Engine
