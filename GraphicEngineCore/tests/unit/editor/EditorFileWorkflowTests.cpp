#include "application/include/EditorProjectWorkflow.h"
#include "application/include/EditorSceneDocument.h"
#include "assets/include/AssetManager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {
class TemporaryProjectDirectory final {
public:
  TemporaryProjectDirectory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    m_Root = std::filesystem::temp_directory_path() / ("GraphicEngineEditorWorkflowTests-" + std::to_string(nonce));
    std::filesystem::create_directories(m_Root / "initial");
    std::filesystem::create_directories(m_Root / "selected");
  }

  TemporaryProjectDirectory(const TemporaryProjectDirectory&) = delete;
  auto operator=(const TemporaryProjectDirectory&) -> TemporaryProjectDirectory& = delete;
  ~TemporaryProjectDirectory() noexcept {
    std::error_code ignored;
    std::filesystem::remove_all(m_Root, ignored);
  }

  [[nodiscard]] auto Root() const noexcept -> const std::filesystem::path& { return m_Root; }

private:
  std::filesystem::path m_Root;
};

TEST(EditorProjectWorkflowTests, CancelKeepsCurrentProject) {
  TemporaryProjectDirectory directory;
  Engine::AssetManager assets(directory.Root() / "initial");
  const auto previousRoot = assets.GetProjectRoot();
  std::vector<std::string> warnings;
  std::string error;

  EXPECT_EQ(EditorWorkflow::OpenProject(assets, std::nullopt, warnings, error),
            EditorWorkflow::ProjectOpenResult::Cancelled);
  EXPECT_EQ(assets.GetProjectRoot(), previousRoot);
  EXPECT_TRUE(error.empty());
}

TEST(EditorProjectWorkflowTests, OpensSelectedProjectAndRefreshesItsRegistry) {
  TemporaryProjectDirectory directory;
  Engine::AssetManager assets(directory.Root() / "initial");
  std::vector<std::string> warnings;
  std::string error;

  EXPECT_EQ(EditorWorkflow::OpenProject(assets, directory.Root() / "selected", warnings, error),
            EditorWorkflow::ProjectOpenResult::Opened);
  EXPECT_EQ(assets.GetProjectRoot(), std::filesystem::weakly_canonical(directory.Root() / "selected"));
  EXPECT_TRUE(error.empty());
}

TEST(EditorProjectWorkflowTests, InvalidProjectLeavesCurrentProjectActive) {
  TemporaryProjectDirectory directory;
  Engine::AssetManager assets(directory.Root() / "initial");
  const auto previousRoot = assets.GetProjectRoot();
  std::vector<std::string> warnings;
  std::string error;

  EXPECT_EQ(EditorWorkflow::OpenProject(assets, directory.Root() / "missing", warnings, error),
            EditorWorkflow::ProjectOpenResult::Failed);
  EXPECT_EQ(assets.GetProjectRoot(), previousRoot);
  EXPECT_FALSE(error.empty());
}

TEST(EditorSceneDocumentTests, CancelDoesNotChangeActiveDocumentPath) {
  EditorSceneDocument activeDocument;
  const auto original = std::filesystem::temp_directory_path() / "original.scene.json";
  activeDocument.CommitPath(original);
  const auto previousPath = activeDocument.GetPath();
  std::string error;
  std::vector<std::string> warnings;

  EXPECT_EQ(activeDocument.Open(
                std::nullopt,
                [](const auto&, auto&, auto&) -> bool {
                  ADD_FAILURE() << "cancel invoked loader";
                  return false;
                },
                error, warnings),
            EditorSceneDocument::OpenResult::Cancelled);
  EXPECT_EQ(activeDocument.GetPath(), previousPath);
}

TEST(EditorSceneDocumentTests, CommitsPathOnlyAfterSceneActivationSucceeds) {
  EditorSceneDocument activeDocument;
  const auto original = std::filesystem::temp_directory_path() / "original.scene.json";
  const auto selected = std::filesystem::temp_directory_path() / "selected.scene.json";
  activeDocument.CommitPath(original);
  std::string error;
  std::vector<std::string> warnings;

  EXPECT_EQ(activeDocument.Open(
                selected, [](const auto&, auto&, auto&) -> bool { return true; }, error, warnings),
            EditorSceneDocument::OpenResult::Opened);
  EXPECT_EQ(activeDocument.GetPath(), std::filesystem::weakly_canonical(selected));
  EXPECT_TRUE(error.empty());
}

TEST(EditorSceneDocumentTests, FailedSceneActivationKeepsExistingPathAndDiagnostic) {
  EditorSceneDocument activeDocument;
  const auto original = std::filesystem::temp_directory_path() / "original.scene.json";
  const auto selected = std::filesystem::temp_directory_path() / "broken.scene.json";
  activeDocument.CommitPath(original);
  const auto previousPath = activeDocument.GetPath();
  std::string error;
  std::vector<std::string> warnings;

  EXPECT_EQ(activeDocument.Open(
                selected,
                [](const auto&, auto& loadError, auto&) -> bool {
                  loadError = "scene document could not be read";
                  return false;
                },
                error, warnings),
            EditorSceneDocument::OpenResult::Failed);
  EXPECT_EQ(activeDocument.GetPath(), previousPath);
  EXPECT_EQ(error, "scene document could not be read");
}
} // namespace
