#pragma once

#include "panels/include/EditorPanel.h"
#include "platform/include/EditorFileDialogs.h"

#include <functional>
#include <utility>

namespace Engine {
class Application;
}

class EngineSettingsPanel final : public EditorPanel {
public:
  EngineSettingsPanel(Engine::Application* application,
                      std::function<EditorPlatform::EditorPreferences()> getPreferences,
                      std::function<void(const EditorPlatform::EditorPreferences&)> setPreferences)
      : m_Application(application), m_GetPreferences(std::move(getPreferences)),
        m_SetPreferences(std::move(setPreferences)) {}

  void Draw() override;
  void Open() noexcept { m_Open = true; }

private:
  Engine::Application* m_Application = nullptr;
  bool m_Open = false;
  std::function<EditorPlatform::EditorPreferences()> m_GetPreferences;
  std::function<void(const EditorPlatform::EditorPreferences&)> m_SetPreferences;
};
