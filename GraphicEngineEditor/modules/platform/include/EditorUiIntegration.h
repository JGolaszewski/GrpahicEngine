#pragma once

namespace Engine
{
class IWindow;
class IRenderBackend;
class ViewportTarget;
} // namespace Engine

class EditorUiIntegration
{
  public:
    bool Initialize(Engine::IWindow *window, Engine::IRenderBackend *backend);
    void BeginFrame();
    void EndFrame();
    void Shutdown();
    void *GetTextureId(const Engine::ViewportTarget *target) const noexcept;
    bool IsInitialized() const noexcept
    {
        return m_Initialized;
    }

  private:
    Engine::IRenderBackend *m_Backend = nullptr;
    bool m_Initialized = false;
};
