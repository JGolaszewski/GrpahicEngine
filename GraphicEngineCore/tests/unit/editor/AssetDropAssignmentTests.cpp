#include "panels/include/AssetDragDropPayload.h"
#include "panels/include/AssetDropAssignment.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {
constexpr char kModelGuid[] = "11111111-2222-4333-8444-555555555555";
constexpr char kTextureGuid[] = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";

TEST(AssetDropAssignmentTests, ModelDropSwitchesMeshToRegisteredAsset) {
  Engine::Ecs::MeshRenderer mesh{.ModelGuid = "old-model", .PrimitiveType = "sphere"};
  const std::vector<Engine::AssetManager::ModelAssetInfo> models = {{.guid = kModelGuid, .name = "Model"}};
  std::string error;

  ASSERT_TRUE(AssignDroppedModel(mesh, kModelGuid, models, error)) << error;
  EXPECT_EQ(mesh.ModelGuid, kModelGuid);
  EXPECT_TRUE(mesh.PrimitiveType.empty());
  EXPECT_TRUE(error.empty());
}

TEST(AssetDropAssignmentTests, UnknownModelDropLeavesMeshUnchanged) {
  Engine::Ecs::MeshRenderer mesh{.ModelGuid = "existing-model", .PrimitiveType = "cube"};
  const auto original = mesh;
  std::string error;

  EXPECT_FALSE(AssignDroppedModel(mesh, kModelGuid, {}, error));
  EXPECT_EQ(mesh.ModelGuid, original.ModelGuid);
  EXPECT_EQ(mesh.PrimitiveType, original.PrimitiveType);
  EXPECT_FALSE(error.empty());
}

TEST(AssetDropAssignmentTests, TextureDropsAssignBothSupportedSlots) {
  Engine::SceneMaterialDocument material;
  const std::vector<Engine::AssetManager::TextureAssetInfo> textures = {{.guid = kTextureGuid, .name = "Texture"}};
  std::string error;

  ASSERT_TRUE(AssignDroppedTexture(material, MaterialTextureSlot::BaseColor, kTextureGuid, textures, error)) << error;
  ASSERT_TRUE(AssignDroppedTexture(material, MaterialTextureSlot::Normal, kTextureGuid, textures, error)) << error;
  EXPECT_EQ(material.baseColorTextureGuid, kTextureGuid);
  EXPECT_EQ(material.normalTextureGuid, kTextureGuid);
}

TEST(AssetDropAssignmentTests, UnknownTextureDropLeavesBothSlotsUnchanged) {
  Engine::SceneMaterialDocument material;
  material.baseColorTextureGuid = "existing-color";
  material.normalTextureGuid = "existing-normal";
  const auto original = material;
  std::string error;

  EXPECT_FALSE(AssignDroppedTexture(material, MaterialTextureSlot::BaseColor, kTextureGuid, {}, error));
  EXPECT_EQ(material.baseColorTextureGuid, original.baseColorTextureGuid);
  EXPECT_EQ(material.normalTextureGuid, original.normalTextureGuid);
  EXPECT_FALSE(error.empty());

  EXPECT_FALSE(AssignDroppedTexture(material, MaterialTextureSlot::Normal, kTextureGuid, {}, error));
  EXPECT_EQ(material.baseColorTextureGuid, original.baseColorTextureGuid);
  EXPECT_EQ(material.normalTextureGuid, original.normalTextureGuid);
  EXPECT_FALSE(error.empty());
}

TEST(AssetDropAssignmentTests, MeshUsesDefaultMaterialUntilAnAssetIsAssigned) {
  Engine::Ecs::MeshRenderer mesh;
  EXPECT_TRUE(mesh.MaterialGuid.empty());
  EXPECT_FALSE(mesh.Material.has_value());

  mesh.MaterialGuid = "material-guid";
  mesh.Material.emplace();
  UseDefaultMaterial(mesh);
  EXPECT_TRUE(mesh.MaterialGuid.empty());
  EXPECT_FALSE(mesh.Material.has_value());
}

TEST(AssetDropAssignmentTests, ReusableMaterialCreationCanBeAssignedAndSharedEditsPropagate) {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root =
      std::filesystem::temp_directory_path() / ("GraphicEngineMaterialAssignmentTests-" + std::to_string(nonce));
  std::filesystem::create_directories(root);
  struct Cleanup final {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  } cleanup{root};
  Engine::AssetManager assets(root);
  const auto result = assets.CreateMaterial("Custom");
  ASSERT_TRUE(result) << result.error().Message;
  const std::string& guid = result.value();
  std::string error;
  ASSERT_TRUE(assets.LoadMaterial(guid, error)) << error;
  const auto createdAsset = assets.GetMaterial(guid);
  ASSERT_TRUE(createdAsset);
  const Engine::SceneMaterialDocument& created = *createdAsset;
  Engine::Ecs::MeshRenderer first;
  Engine::Ecs::MeshRenderer second;
  Engine::Ecs::MeshRenderer unrelated;
  AssignMaterialAsset(first, guid, created);
  AssignMaterialAsset(second, guid, created);
  AssignMaterialAsset(unrelated, "another-material", created);
  EXPECT_EQ(first.MaterialGuid, guid);
  EXPECT_EQ(first.Material, created);
  EXPECT_EQ(second.Material, created);

  Engine::SceneMaterialDocument edited = created;
  edited.roughness = 0.2F;
  EXPECT_TRUE(ApplyMaterialAssetEdit(first, guid, edited));
  EXPECT_TRUE(ApplyMaterialAssetEdit(second, guid, edited));
  EXPECT_FALSE(ApplyMaterialAssetEdit(unrelated, guid, edited));
  EXPECT_EQ(first.Material, edited);
  EXPECT_EQ(second.Material, edited);
  EXPECT_EQ(unrelated.Material, created);
}

TEST(AssetDropAssignmentTests, LegacyEmbeddedMaterialRemainsAvailableWithoutAssetIdentity) {
  Engine::Ecs::MeshRenderer mesh;
  Engine::SceneMaterialDocument legacy;
  legacy.baseColor = {0.2F, 0.4F, 0.6F, 1.0F};
  mesh.Material = legacy;

  EXPECT_TRUE(mesh.MaterialGuid.empty());
  EXPECT_TRUE(mesh.Material.has_value());
  EXPECT_EQ(mesh.Material, legacy);
}

TEST(AssetDropAssignmentTests, DragPayloadRequiresAWellFormedGuidAndFixedSize) {
  const auto payload = MakeAssetDragDropPayload(kModelGuid);
  ASSERT_TRUE(payload);
  const auto decoded = ReadAssetDragDropGuid(&*payload, static_cast<int>(sizeof(*payload)));
  ASSERT_TRUE(decoded);
  EXPECT_EQ(*decoded, kModelGuid);
  EXPECT_FALSE(MakeAssetDragDropPayload("bad-guid"));
  EXPECT_FALSE(ReadAssetDragDropGuid(&*payload, static_cast<int>(sizeof(*payload) - 1)));
}

TEST(AssetDropAssignmentTests, DragPayloadRejectsMalformedGuidBytes) {
  auto payload = MakeAssetDragDropPayload(kModelGuid);
  ASSERT_TRUE(payload);
  payload->Guid[0] = 'x';

  EXPECT_FALSE(ReadAssetDragDropGuid(&*payload, static_cast<int>(sizeof(*payload))));
}
} // namespace
