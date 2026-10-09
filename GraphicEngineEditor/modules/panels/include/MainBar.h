#pragma once

#include "panels/include/EditorPanel.h"

#include <functional>
#include <utility>

class MainBar : public EditorPanel
{
  public:
    void Draw() override;
    void SetConsoleOpenHandler(std::function<void()> handler)
    {
        m_ConsoleOpenHandler = std::move(handler);
    }
    void SetSettingsOpenHandler(std::function<void()> handler)
    {
        m_SettingsOpenHandler = std::move(handler);
    }

  private:
    std::function<void()> m_ConsoleOpenHandler;
    std::function<void()> m_SettingsOpenHandler;
};
