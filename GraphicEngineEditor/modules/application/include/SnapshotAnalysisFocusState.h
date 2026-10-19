#pragma once

#include "scene/include/Scene.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

/// Editor state needed to restore the view after leaving Snapshot Analysis focus mode.
struct SnapshotAnalysisFocusRestoreState {
  std::shared_ptr<Engine::Scene> ActiveScene;                        ///< Scene active before entering focus mode.
  Engine::EntityHandle SelectedEntity = Engine::InvalidEntityHandle; ///< Selection to restore on exit.
  std::filesystem::path SceneDocumentPath;                           ///< Active scene document path to restore.
  std::string FocusedWindow; ///< ImGui window that should regain focus after the editor layout returns.
};

/// Captures and restores editor state around the dedicated Snapshot Analysis view.
class SnapshotAnalysisFocusState final {
public:
  /// Enters focus mode and stores the editor state once.
  /// @param activeScene The scene active before entering focus mode.
  /// @param selectedEntity The selected entity to restore on exit.
  /// @param sceneDocumentPath The active scene document path to restore.
  /// @param focusedWindow ImGui window that should regain focus on exit.
  /// @return True when focus mode was entered, false when already active.
  auto Enter(std::shared_ptr<Engine::Scene> activeScene, Engine::EntityHandle selectedEntity,
             std::filesystem::path sceneDocumentPath, std::string focusedWindow) -> bool {
    if (m_PreviousState) {
      return false;
    }
    m_PreviousState.emplace(SnapshotAnalysisFocusRestoreState{std::move(activeScene), selectedEntity,
                                                              std::move(sceneDocumentPath), std::move(focusedWindow)});
    return true;
  }

  /// Leaves focus mode and returns the captured editor state exactly once.
  /// @return State to restore, or no value when focus mode was inactive.
  auto Exit() -> std::optional<SnapshotAnalysisFocusRestoreState> {
    std::optional<SnapshotAnalysisFocusRestoreState> previous = std::move(m_PreviousState);
    m_PreviousState.reset();
    return previous;
  }

  /// @return Whether the dedicated Snapshot Analysis view currently owns the editor content area.
  auto IsActive() const noexcept -> bool { return m_PreviousState.has_value(); }

private:
  std::optional<SnapshotAnalysisFocusRestoreState> m_PreviousState;
};
