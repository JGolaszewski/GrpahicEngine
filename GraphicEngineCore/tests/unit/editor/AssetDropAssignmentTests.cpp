#include "panels/include/AssetDragDropPayload.h"
#include "panels/include/AssetDropAssignment.h"

#include <gtest/gtest.h>

#include <string>
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

TEST(AssetDropAssignmentTests, DragPayloadRequiresAWellFormedGuidAndFixedSize) {
  const auto payload = MakeAssetDragDropPayload(kModelGuid);
  ASSERT_TRUE(payload);
  const auto decoded = ReadAssetDragDropGuid(&*payload, static_cast<int>(sizeof(*payload)));
  ASSERT_TRUE(decoded);
  EXPECT_EQ(*decoded, kModelGuid);
  EXPECT_FALSE(MakeAssetDragDropPayload("bad-guid"));
  EXPECT_FALSE(ReadAssetDragDropGuid(&*payload, static_cast<int>(sizeof(*payload) - 1)));
}
} // namespace
