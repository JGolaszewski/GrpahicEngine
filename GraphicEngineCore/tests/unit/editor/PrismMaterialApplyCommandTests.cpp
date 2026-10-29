#include "application/src/PrismMaterialApplyCommand.h"
#include "commands/include/CommandHistory.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {
class TemporaryProjectDirectory final {
public:
  TemporaryProjectDirectory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    m_Root = std::filesystem::temp_directory_path() / ("GraphicEnginePrismApplyTests-" + std::to_string(nonce));
    std::filesystem::create_directories(m_Root);
  }
  ~TemporaryProjectDirectory() noexcept { // NOLINT(bugprone-exception-escape): uses remove_all's error_code overload.
    std::error_code ignored;
    std::filesystem::remove_all(m_Root, ignored);
  }
  TemporaryProjectDirectory(const TemporaryProjectDirectory&) = delete;
  auto operator=(const TemporaryProjectDirectory&) -> TemporaryProjectDirectory& = delete;
  [[nodiscard]] auto Root() const noexcept -> const std::filesystem::path& { return m_Root; }

private:
  std::filesystem::path m_Root;
};

struct ApplyFixture final {
  TemporaryProjectDirectory project;
  Engine::AssetManager assets{project.Root()};
  Engine::SceneMaterialDocument before;
  Engine::SceneMaterialDocument after;
  std::string guid;
  std::shared_ptr<Engine::Scene> scene = std::make_shared<Engine::Scene>();
  Engine::EntityHandle first = Engine::InvalidEntityHandle;
  Engine::EntityHandle second = Engine::InvalidEntityHandle;
  Engine::EntityHandle unrelated = Engine::InvalidEntityHandle;

  ApplyFixture() {
    before.brdfModel = "ggx";
    before.roughness = 0.72F;
    after = before;
    after.brdfModel = "prismNdf";
    after.roughness = 0.31F;
    after.generatedNdf.emplace();
    after.generatedNdf->data.residuals.resize(Engine::PrismNdfSampleCount);
    after.generatedNdf->prompt = "brushed silver with fine directional grooves";
    after.generatedNdf->data.zernikeCoefficients[0] = 0.8F;
    after.generatedNdf->data.residuals[17] = -0.025F;

    const auto created = assets.CreateMaterial("PRISM Apply Test", before);
    if (!created) {
      throw std::runtime_error(created.error().Message);
    }
    guid = created.value();
    first = CreateMaterialUser(guid, before);
    second = CreateMaterialUser(guid, before);
    unrelated = CreateMaterialUser("unrelated-material-guid", before);
  }

  auto CreateMaterialUser(const std::string& materialGuid, const Engine::SceneMaterialDocument& material)
      -> Engine::EntityHandle {
    (void)scene->QueueCreate("Material user", Engine::InvalidEntityHandle, Engine::Scene::EntityPreset::Mesh);
    const auto results = scene->sync();
    if (results.empty() || !results.back().Outcome) {
      throw std::runtime_error("could not create mesh entity for PRISM command test");
    }
    const auto entity = results.back().Entity;
    auto* mesh = scene->World().Get<Engine::Ecs::MeshRenderer>(entity);
    if (mesh == nullptr) {
      throw std::runtime_error("mesh preset omitted its renderer component");
    }
    auto assigned = *mesh;
    assigned.MaterialGuid = materialGuid;
    assigned.Material = material;
    if (!scene->SetInPlace(entity, std::move(assigned))) {
      throw std::runtime_error("could not assign mesh material for PRISM command test");
    }
    return entity;
  }

  [[nodiscard]] auto Mesh(Engine::EntityHandle entity) const -> const Engine::Ecs::MeshRenderer& {
    const auto* mesh = scene->World().Get<Engine::Ecs::MeshRenderer>(entity);
    if (mesh == nullptr) {
      throw std::runtime_error("expected material user mesh is missing");
    }
    return *mesh;
  }
};

void ExpectMaterialState(const Engine::SceneMaterialDocument& actual, const Engine::SceneMaterialDocument& expected) {
  EXPECT_EQ(actual.brdfModel, expected.brdfModel);
  EXPECT_FLOAT_EQ(actual.roughness, expected.roughness);
  EXPECT_EQ(actual.generatedNdf.has_value(), expected.generatedNdf.has_value());
  if (actual.generatedNdf && expected.generatedNdf) {
    EXPECT_EQ(actual.generatedNdf->prompt, expected.generatedNdf->prompt);
    EXPECT_FLOAT_EQ(actual.generatedNdf->data.zernikeCoefficients[0],
                    expected.generatedNdf->data.zernikeCoefficients[0]);
    EXPECT_FLOAT_EQ(actual.generatedNdf->data.residuals[17], expected.generatedNdf->data.residuals[17]);
  }
}

void ExpectMeshMaterialState(const ApplyFixture& fixture, Engine::EntityHandle entity,
                             const Engine::SceneMaterialDocument& expected) {
  const auto& mesh = fixture.Mesh(entity);
  ASSERT_TRUE(mesh.Material);
  ExpectMaterialState(*mesh.Material, expected);
}

TEST(PrismMaterialApplyCommandTests, AtomicSaveFailureLeavesMaterialSharedUsersAndHistoryUntouched) {
  ApplyFixture fixture;
  std::string error;
  ASSERT_TRUE(fixture.assets.LoadMaterial(fixture.guid, error)) << error;
  const auto materials = fixture.assets.GetKnownMaterials();
  const auto material = std::ranges::find(materials, fixture.guid, &Engine::AssetManager::MaterialAssetInfo::guid);
  ASSERT_NE(material, materials.end());
  const auto sourcePath = fixture.project.Root() / material->sourcePath;
  std::error_code filesystemError;
  ASSERT_TRUE(std::filesystem::remove(sourcePath, filesystemError)) << filesystemError.message();
  ASSERT_TRUE(std::filesystem::create_directory(sourcePath, filesystemError)) << filesystemError.message();

  CommandHistory history;
  ASSERT_FALSE(history.Execute(std::make_unique<PrismMaterialApplyCommand>(
      fixture.assets, fixture.scene, fixture.guid, fixture.before, fixture.after, std::function<void()>{})));

  EXPECT_FALSE(history.CanUndo());
  EXPECT_FALSE(history.CanRedo());
  EXPECT_EQ(history.GetUndoStackSize(), 0U);
  EXPECT_EQ(history.GetRedoStackSize(), 0U);
  ExpectMeshMaterialState(fixture, fixture.first, fixture.before);
  ExpectMeshMaterialState(fixture, fixture.second, fixture.before);
  EXPECT_EQ(fixture.Mesh(fixture.first).MaterialGuid, fixture.guid);
  EXPECT_EQ(fixture.Mesh(fixture.second).MaterialGuid, fixture.guid);
  EXPECT_EQ(fixture.Mesh(fixture.unrelated).MaterialGuid, "unrelated-material-guid");
  const auto cached = fixture.assets.GetMaterial(fixture.guid);
  ASSERT_TRUE(cached);
  ExpectMaterialState(*cached, fixture.before);
}

TEST(PrismMaterialApplyCommandTests, ApplyUndoRedoUpdatesOneSharedMaterialAndOneHistoryEntry) {
  ApplyFixture fixture;
  CommandHistory history;
  int sceneChangedCount = 0;
  ASSERT_TRUE(history.Execute(
      std::make_unique<PrismMaterialApplyCommand>(fixture.assets, fixture.scene, fixture.guid, fixture.before,
                                                  fixture.after, [&sceneChangedCount] { ++sceneChangedCount; })));
  ASSERT_EQ(history.GetUndoStackSize(), 1U);
  EXPECT_EQ(history.GetRedoStackSize(), 0U);
  ExpectMeshMaterialState(fixture, fixture.first, fixture.after);
  ExpectMeshMaterialState(fixture, fixture.second, fixture.after);
  EXPECT_EQ(fixture.Mesh(fixture.unrelated).MaterialGuid, "unrelated-material-guid");
  auto persisted = fixture.assets.GetMaterial(fixture.guid);
  ASSERT_TRUE(persisted);
  ExpectMaterialState(*persisted, fixture.after);

  ASSERT_TRUE(history.Undo());
  EXPECT_EQ(history.GetUndoStackSize(), 0U);
  EXPECT_EQ(history.GetRedoStackSize(), 1U);
  ExpectMeshMaterialState(fixture, fixture.first, fixture.before);
  ExpectMeshMaterialState(fixture, fixture.second, fixture.before);
  persisted = fixture.assets.GetMaterial(fixture.guid);
  ASSERT_TRUE(persisted);
  ExpectMaterialState(*persisted, fixture.before);

  ASSERT_TRUE(history.Redo());
  EXPECT_EQ(history.GetUndoStackSize(), 1U);
  EXPECT_EQ(history.GetRedoStackSize(), 0U);
  ExpectMeshMaterialState(fixture, fixture.first, fixture.after);
  ExpectMeshMaterialState(fixture, fixture.second, fixture.after);
  persisted = fixture.assets.GetMaterial(fixture.guid);
  ASSERT_TRUE(persisted);
  ExpectMaterialState(*persisted, fixture.after);
  EXPECT_EQ(sceneChangedCount, 3);
}
} // namespace
