#pragma once

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
  bool Initialize(Engine::IWindow* window, Engine::IRenderBackend* backend);
  void BeginFrame() const;
  void EndFrame();
  void Shutdown();
  void* GetTextureId(const Engine::ViewportTarget* target) const noexcept;
  bool IsInitialized() const noexcept { return m_Initialized; }

private:
  Engine::D3D11Backend* m_Backend = nullptr;
  bool m_Initialized = false;
};
