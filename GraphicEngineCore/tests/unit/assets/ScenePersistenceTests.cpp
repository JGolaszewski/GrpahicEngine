#include "assets/include/AssetManager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {

class TemporaryProject final {
public:
  TemporaryProject() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    m_Root = std::filesystem::temp_directory_path() / ("GraphicEngineScenePersistenceTests-" + std::to_string(nonce));
    std::filesystem::create_directories(m_Root);
  }

  TemporaryProject(const TemporaryProject&) = delete;
  auto operator=(const TemporaryProject&) -> TemporaryProject& = delete;
  TemporaryProject(TemporaryProject&&) = delete;
  auto operator=(TemporaryProject&&) -> TemporaryProject& = delete;

  ~TemporaryProject() noexcept {
    std::error_code ignored;
    try {
      std::filesystem::remove_all(m_Root, ignored);
    } catch (...) {
    }
  }

  [[nodiscard]] auto Root() const noexcept -> const std::filesystem::path& { return m_Root; }

private:
  std::filesystem::path m_Root;
};

TEST(ScenePersistenceTests, RoundTripsFieldsAndRejectsTraversalWithoutOverwritingValidFile) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  Engine::SceneDocument document;
  document.unitsPerMeter = 0.25F;
  Engine::SceneEntityDocument entity;
  entity.id = 23;
  entity.name = "persisted entity";
  entity.position = {1.0F, -2.0F, 3.5F};
  entity.forceWireframe = true;
  document.entities.push_back(entity);

  std::string error;
  ASSERT_TRUE(assets.SaveScene("level.scene.json", document, error)) << error;
  const auto originalBytes = [&project] {
    std::ifstream input(project.Root() / "level.scene.json", std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  }();

  EXPECT_FALSE(assets.SaveScene("../escaped.scene.json", document, error));
  EXPECT_EQ(originalBytes, [&project] {
    std::ifstream input(project.Root() / "level.scene.json", std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  }());

  const auto loaded = assets.LoadScene("level.scene.json");
  ASSERT_TRUE(loaded) << loaded.error;
  ASSERT_EQ(loaded.document.entities.size(), 1U);
  EXPECT_FLOAT_EQ(loaded.document.unitsPerMeter, 0.25F);
  EXPECT_EQ(loaded.document.entities.front().id, 23U);
  EXPECT_EQ(loaded.document.entities.front().name, "persisted entity");
  EXPECT_EQ(loaded.document.entities.front().position, (std::array<float, 3>{1.0F, -2.0F, 3.5F}));
  EXPECT_TRUE(loaded.document.entities.front().forceWireframe);
}

TEST(ScenePersistenceTests, MalformedInputReturnsDiagnostic) {
  TemporaryProject project;
  Engine::AssetManager assets(project.Root());
  {
    std::ofstream malformed(project.Root() / "bad.scene.json", std::ios::binary);
    malformed << "{not valid JSON";
  }

  const auto loaded = assets.LoadScene("bad.scene.json");
  EXPECT_FALSE(loaded);
  EXPECT_NE(loaded.error.find("bad.scene.json"), std::string::npos);
}

} // namespace
