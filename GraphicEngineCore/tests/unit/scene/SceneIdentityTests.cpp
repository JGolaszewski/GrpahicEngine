#include "scene/include/Scene.h"

#include <gtest/gtest.h>

TEST(SceneIdentityTests, StructuralChangesAreStagedAndHandlesBecomeStaleAfterDestruction) {
  Engine::Scene scene;
  const uint64_t sequence = scene.QueueCreate("Persistent entity");
  EXPECT_EQ(scene.EntityCount(), 1U);

  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 1U);
  ASSERT_EQ(created.front().Sequence, sequence);
  ASSERT_TRUE(created.front().Outcome);
  const Engine::EntityHandle entity = created.front().Entity;
  ASSERT_TRUE(scene.World().IsAlive(entity));
  ASSERT_NE(scene.World().Get<Engine::Ecs::Name>(entity), nullptr);
  EXPECT_EQ(scene.World().Get<Engine::Ecs::Name>(entity)->Value, "Persistent entity");

  const uint64_t destroySequence = scene.QueueDestroy(entity);
  EXPECT_TRUE(scene.World().IsAlive(entity));
  const auto destroyed = scene.sync();
  ASSERT_EQ(destroyed.size(), 1U);
  EXPECT_EQ(destroyed.front().Sequence, destroySequence);
  EXPECT_TRUE(destroyed.front().Outcome);
  EXPECT_FALSE(scene.World().IsAlive(entity));
}

TEST(SceneIdentityTests, SceneRootIsPermanentAndOwnsTopLevelEntities) {
  Engine::Scene scene;
  const Engine::EntityHandle root = scene.SceneRootEntity();
  ASSERT_TRUE(scene.World().IsAlive(root));
  ASSERT_TRUE(scene.IsSceneRoot(root));

  (void)scene.QueueCreate("Child");
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 1U);
  ASSERT_TRUE(created.front().Outcome);
  const Engine::EntityHandle child = created.front().Entity;
  ASSERT_NE(scene.World().Get<Engine::Ecs::Parent>(child), nullptr);
  EXPECT_EQ(scene.World().Get<Engine::Ecs::Parent>(child)->Id, root);

  (void)scene.QueueDestroy(root);
  const auto destroyed = scene.sync();
  ASSERT_EQ(destroyed.size(), 1U);
  EXPECT_FALSE(destroyed.front().Outcome);
  EXPECT_TRUE(scene.World().IsAlive(root));

  (void)scene.QueueReparent(root, Engine::InvalidEntityHandle);
  const auto reparented = scene.sync();
  ASSERT_EQ(reparented.size(), 1U);
  EXPECT_FALSE(reparented.front().Outcome);
}

TEST(SceneIdentityTests, RootRejectsRenderableAndAuthoredComponents) {
  Engine::Scene scene;
  const Engine::EntityHandle root = scene.SceneRootEntity();
  (void)scene.QueueAdd(root, Engine::Ecs::MeshRenderer{});
  (void)scene.QueueAdd(root, Engine::Ecs::SpotLight{});
  (void)scene.QueueAdd(root, Engine::Ecs::Camera{});
  const auto results = scene.sync();
  ASSERT_EQ(results.size(), 3U);
  for (const auto& result : results) {
    EXPECT_FALSE(result.Outcome);
  }
  EXPECT_FALSE(scene.World().Has<Engine::Ecs::MeshRenderer>(root));
  EXPECT_FALSE(scene.World().Has<Engine::Ecs::SpotLight>(root));
  EXPECT_FALSE(scene.World().Has<Engine::Ecs::Camera>(root));
}

TEST(SceneIdentityTests, PersistentIdentityIsStoredAsAnEcsComponent) {
  Engine::Scene scene;
  (void)scene.QueueCreate("Entity");
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 1U);
  ASSERT_TRUE(created.front().Outcome);
  const Engine::EntityHandle entity = created.front().Entity;

  constexpr Engine::Ecs::SceneGuid guid{.High = 7, .Low = 42};
  (void)scene.QueueSet(entity, Engine::Ecs::PersistentIdentity{.Guid = guid});
  const auto updated = scene.sync();
  ASSERT_EQ(updated.size(), 1U);
  ASSERT_TRUE(updated.front().Outcome);
  const auto* identity = scene.World().Get<Engine::Ecs::PersistentIdentity>(entity);
  ASSERT_NE(identity, nullptr);
  EXPECT_EQ(identity->Guid, guid);
}
