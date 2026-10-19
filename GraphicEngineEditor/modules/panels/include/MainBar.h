#pragma once

#include "panels/include/EditorPanel.h"

#include <functional>
#include <utility>

class MainBar : public EditorPanel {
public:
  void Draw() override;
  void SetConsoleOpenHandler(std::function<void()> handler) { m_ConsoleOpenHandler = std::move(handler); }
  void SetSettingsOpenHandler(std::function<void()> handler) { m_SettingsOpenHandler = std::move(handler); }
  /// Sets the callback used to enter or exit the focused Snapshot Analysis workspace.
  /// @param handler Receives true to enter and false to exit focus mode.
  void SetSnapshotAnalysisFocusHandler(std::function<void(bool)> handler) {
    m_SnapshotAnalysisFocusHandler = std::move(handler);
  }
  /// Updates the Main Bar controls for the active Snapshot Analysis view mode.
  /// @param focused Whether the focused workspace currently owns the editor view.
  void SetSnapshotAnalysisFocused(bool focused) noexcept { m_SnapshotAnalysisFocused = focused; }

private:
  void DrawFileMenu();
  void DrawWindowMenu();
  std::function<void()> m_ConsoleOpenHandler;
  std::function<void()> m_SettingsOpenHandler;
  std::function<void(bool)> m_SnapshotAnalysisFocusHandler;
  bool m_SnapshotAnalysisFocused = false;
};
