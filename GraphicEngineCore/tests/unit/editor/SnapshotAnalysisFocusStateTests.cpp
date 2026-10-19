#include "application/include/SnapshotAnalysisFocusState.h"

#include <gtest/gtest.h>

#include <memory>

TEST(SnapshotAnalysisFocusStateTests, EnterAndExitRestoreSceneAndFocusedWindowExactlyOnce) {
  SnapshotAnalysisFocusState focusState;
  auto scene = std::make_shared<Engine::Scene>();
  const Engine::EntityHandle selectedEntity{.Index = 17U, .Generation = 4U};
  const std::filesystem::path scenePath = "scenes/active.scene";

  EXPECT_TRUE(focusState.Enter(scene, selectedEntity, scenePath, "Scene View"));
  EXPECT_TRUE(focusState.IsActive());
  EXPECT_FALSE(focusState.Enter(std::make_shared<Engine::Scene>(), Engine::InvalidEntityHandle, {}, "Hierarchy"));

  auto restored = focusState.Exit();
  ASSERT_TRUE(restored.has_value());
  EXPECT_FALSE(focusState.IsActive());
  EXPECT_EQ(restored->ActiveScene, scene);
  EXPECT_EQ(restored->SelectedEntity, selectedEntity);
  EXPECT_EQ(restored->SceneDocumentPath, scenePath);
  EXPECT_EQ(restored->FocusedWindow, "Scene View");
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
