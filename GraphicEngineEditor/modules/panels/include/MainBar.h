#pragma once

#include "panels/include/EditorPanel.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

class MainBar : public EditorPanel {
public:
  /// A View menu entry backed by a panel's current visibility.
  struct PanelMenuItem {
    std::string Label;
    std::function<bool()> IsOpen;
    std::function<void(bool)> SetOpen;
  };

  void Draw() override;
  /// Replaces the panel entries shown under View.
  void SetPanelMenuItems(std::vector<PanelMenuItem> items) { m_PanelMenuItems = std::move(items); }
  /// Sets callbacks for restoring, saving, and loading the dock arrangement.
  void SetLayoutHandlers(std::function<void()> reset, std::function<void()> save, std::function<void()> load) {
    m_ResetLayoutHandler = std::move(reset);
    m_SaveLayoutHandler = std::move(save);
    m_LoadLayoutHandler = std::move(load);
  }

private:
  void DrawFileMenu();
  void DrawViewMenu();
  std::vector<PanelMenuItem> m_PanelMenuItems;
  std::function<void()> m_ResetLayoutHandler;
  std::function<void()> m_SaveLayoutHandler;
  std::function<void()> m_LoadLayoutHandler;
};
