#include "assets/include/AssetManager.h"
#include "runtime/include/Application.h"
#include "scene/include/SceneDocument.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class SandboxProjectCopy {
public:
  SandboxProjectCopy() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    m_Path = std::filesystem::temp_directory_path() / ("GraphicEngineSandboxTest-" + std::to_string(unique));
    std::filesystem::copy(ENGINE_SANDBOX_PROJECT_ROOT, m_Path,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
  }
  ~SandboxProjectCopy() {
    std::error_code error;
    std::filesystem::remove_all(m_Path, error);
  }
  const std::filesystem::path& Path() const noexcept { return m_Path; }

private:
  std::filesystem::path m_Path;
};

TEST(SandboxSceneTests, BundledSceneLoadsAndRestoresRepresentativeEditorContent) {
  SandboxProjectCopy project;
  Engine::Application application;
  Engine::AssetManager* assets = application.GetAssetManager();
  ASSERT_NE(assets, nullptr);
  std::string error;
  ASSERT_TRUE(assets->SetProjectRoot(project.Path(), error)) << error;
  std::vector<std::string> registryWarnings;
  ASSERT_TRUE(assets->RefreshRegistry(registryWarnings, error)) << error;

  const std::filesystem::path templatePath = project.Path() / "Sandbox.template.scene.json";
  const Engine::SceneDocumentResult loaded = assets->LoadScene(templatePath);
  ASSERT_TRUE(loaded) << loaded.error;
  const Engine::SceneDocument& document = loaded.document;
  ASSERT_EQ(document.entities.size(), 10U);
  ASSERT_EQ(document.lights.size(), 3U);
  ASSERT_EQ(document.cameras.size(), 1U);

  size_t modelCount = 0;
  size_t cubeCount = 0;
  size_t sphereCount = 0;
  size_t planeCount = 0;
  size_t coneCount = 0;
  for (const Engine::SceneEntityDocument& entity : document.entities) {
    if (entity.modelGuid.has_value()) {
      ++modelCount;
      ASSERT_TRUE(entity.material.has_value());
      EXPECT_EQ(entity.material->baseColorTextureGuid, "a1000000-0000-4000-8000-000000000002");
    }
    if (entity.primitiveType == "cube") ++cubeCount;
    if (entity.primitiveType == "sphere") ++sphereCount;
    if (entity.primitiveType == "plane") ++planeCount;
    if (entity.primitiveType == "cone") ++coneCount;
  }
  EXPECT_EQ(modelCount, 1U);
  EXPECT_EQ(cubeCount, 1U);
  EXPECT_EQ(sphereCount, 1U);
  EXPECT_EQ(planeCount, 1U);
  EXPECT_EQ(coneCount, 1U);
  size_t directionalCount = 0;
  size_t pointCount = 0;
  size_t spotCount = 0;
  for (const Engine::SceneLightDocument& light : document.lights) {
    if (light.type == "directional") ++directionalCount;
    if (light.type == "point") ++pointCount;
    if (light.type == "spot") ++spotCount;
  }
  EXPECT_EQ(directionalCount, 1U);
  EXPECT_EQ(pointCount, 1U);
  EXPECT_EQ(spotCount, 1U);
  EXPECT_EQ(assets->GetKnownModels().size(), 1U);
  EXPECT_EQ(assets->GetKnownTextures().size(), 1U);

  std::vector<std::string> warnings;
  ASSERT_TRUE(application.LoadSceneDocument(document, error, warnings)) << error;
  ASSERT_EQ(application.GetScenes().size(), 1U);
  const Engine::SceneDocument snapshot = application.CreateSceneDocument(*application.GetScenes().front());
  const std::filesystem::path savePath = project.Path() / "Sandbox.scene.json";
  ASSERT_TRUE(assets->SaveScene(savePath, snapshot, error)) << error;
  const Engine::SceneDocumentResult reopened = assets->LoadScene(savePath);
  ASSERT_TRUE(reopened) << reopened.error;
  EXPECT_EQ(reopened.document.entities.size(), document.entities.size());
  EXPECT_EQ(reopened.document.lights.size(), document.lights.size());
  EXPECT_EQ(reopened.document.cameras.size(), document.cameras.size());

  ASSERT_TRUE(application.LoadSceneDocument(reopened.document, error, warnings)) << error;
  ASSERT_EQ(application.GetScenes().size(), 1U);
  const Engine::SceneDocument restored = application.CreateSceneDocument(*application.GetScenes().front());
  EXPECT_EQ(restored.entities.size(), document.entities.size());
  EXPECT_EQ(restored.lights.size(), document.lights.size());
  EXPECT_EQ(restored.cameras.size(), document.cameras.size());
}

} // namespace
