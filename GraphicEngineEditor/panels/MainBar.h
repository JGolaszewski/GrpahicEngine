#pragma once

#include "EditorPanel.h"

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

  private:
    std::function<void()> m_ConsoleOpenHandler;
};
