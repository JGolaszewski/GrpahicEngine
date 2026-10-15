#pragma once

#include "panels/include/EditorPanel.h"

namespace Engine {
class Application;
}

class EngineSettingsPanel final : public EditorPanel {
public:
  explicit EngineSettingsPanel(Engine::Application* application) : m_Application(application) {}

  void Draw() override;
  void Open() noexcept { m_Open = true; }

private:
  Engine::Application* m_Application = nullptr;
  bool m_Open = false;
};
