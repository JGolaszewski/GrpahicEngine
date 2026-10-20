#include "application/include/SnapshotAnalysisFocusState.h"

#include <gtest/gtest.h>

#include <memory>

TEST(SnapshotAnalysisFocusStateTests, EnterAndExitRestoreMutatedEditorStateThroughProductionRestorePath) {
  SnapshotAnalysisFocusState focusState;
  auto scene = std::make_shared<Engine::Scene>();
  std::shared_ptr<Engine::Scene> activeScene = scene;
  const Engine::EntityHandle selectedEntity{.Index = 17U, .Generation = 4U};
  Engine::EntityHandle currentSelection = selectedEntity;
  std::filesystem::path sceneDocumentPath = "scenes/active.scene";
  std::string focusedWindow = "Scene View";

  EXPECT_TRUE(focusState.Enter(activeScene, currentSelection, sceneDocumentPath, focusedWindow));
  EXPECT_TRUE(focusState.IsActive());
  EXPECT_FALSE(focusState.Enter(std::make_shared<Engine::Scene>(), Engine::InvalidEntityHandle, {}, "Hierarchy"));

  currentSelection = Engine::InvalidEntityHandle;
  sceneDocumentPath = "scenes/changed-during-focus.scene";
  focusedWindow = "Snapshot Analysis##FocusedWorkspace";

  auto restored = focusState.Exit();
  ASSERT_TRUE(restored.has_value());
  EXPECT_FALSE(focusState.IsActive());
  focusState.ApplyRestoreState(*restored, activeScene, currentSelection, sceneDocumentPath, focusedWindow);
  EXPECT_EQ(activeScene, scene);
  EXPECT_EQ(activeScene.get(), scene.get());
  EXPECT_EQ(currentSelection, selectedEntity);
  EXPECT_EQ(sceneDocumentPath, "scenes/active.scene");
  EXPECT_EQ(focusedWindow, "Scene View");
  EXPECT_FALSE(focusState.Exit().has_value());
}

TEST(SnapshotAnalysisFocusStateTests, ClosingFocusModeDoesNotReplaceOrMutateTheActiveScene) {
  SnapshotAnalysisFocusState focusState;
  auto activeScene = std::make_shared<Engine::Scene>();
  const Engine::Scene* activeSceneIdentity = activeScene.get();

  ASSERT_TRUE(focusState.Enter(activeScene, Engine::InvalidEntityHandle, "scene.scene", "Inspector"));
  const auto restored = focusState.Exit();

  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->ActiveScene.get(), activeSceneIdentity);
  EXPECT_EQ(restored->ActiveScene, activeScene);
}

TEST(SnapshotAnalysisFocusStateTests, FocusModeKeepsTheEditorDockspaceAliveWithoutDisplayingIt) {
  EXPECT_EQ(GetSnapshotAnalysisDockspaceFlags(true), ImGuiDockNodeFlags_KeepAliveOnly);
  EXPECT_EQ(GetSnapshotAnalysisDockspaceFlags(false), ImGuiDockNodeFlags_PassthruCentralNode);
}
