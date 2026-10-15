#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace Engine {
class IWindow;
class IRenderBackend;
class ViewportTarget;
} // namespace Engine

namespace Engine {
class D3D11Backend;
}

class EditorUiIntegration {
public:
  EditorUiIntegration();
  ~EditorUiIntegration();
  bool Initialize(Engine::IWindow* window, Engine::IRenderBackend* backend);
  void BeginFrame() const;
  void EndFrame();
  void Shutdown();
  void* GetTextureId(const Engine::ViewportTarget* target) const noexcept;
  /// Uploads top-to-bottom RGBA8 preview pixels for display in the editor.
  bool SetRenderPreview(uint32_t width, uint32_t height, std::span<const uint8_t> rgbaPixels);
  /// Returns the current render-preview shader resource view for ImGui.
  void* GetRenderPreviewTextureId() const noexcept;
  /// Uploads top-to-bottom RGBA8 pixels for the full-resolution camera viewer.
  bool SetCameraPreview(uint32_t width, uint32_t height, std::span<const uint8_t> rgbaPixels);
  /// Returns the current camera-viewer shader resource view for ImGui.
  void* GetCameraPreviewTextureId() const noexcept;
  /// Releases the camera-viewer image texture when its window is closed.
  void ClearCameraPreview() noexcept;
  bool IsInitialized() const noexcept { return m_Initialized; }

private:
  Engine::D3D11Backend* m_Backend = nullptr;
  struct PreviewTexture;
  bool SetPreviewTexture(std::unique_ptr<PreviewTexture>& target, uint32_t width, uint32_t height,
                         std::span<const uint8_t> rgbaPixels);
  std::unique_ptr<PreviewTexture> m_PreviewTexture;
  std::unique_ptr<PreviewTexture> m_CameraPreviewTexture;
  bool m_ClearCameraPreviewAfterDraw = false;
  bool m_Initialized = false;
};
