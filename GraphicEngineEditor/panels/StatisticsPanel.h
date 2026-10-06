#pragma once

#include "EditorPanel.h"

class StatWindow : public EditorPanel
{
  public:
    void Draw() override;

  private:
    float m_FpsHistory[100] = {0.0f};
    float m_TimeHistory[100] = {0.0f};
    int m_HistoryIdx = 0;
};
