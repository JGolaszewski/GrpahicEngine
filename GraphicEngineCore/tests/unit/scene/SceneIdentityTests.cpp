#include "scene/include/Scene.h"

#include <gtest/gtest.h>

TEST(SceneIdentityTests, StructuralChangesAreStagedAndHandlesBecomeStaleAfterDestruction) {
  Engine::Scene scene;
  const uint64_t sequence = scene.QueueCreate("Persistent entity");
  EXPECT_EQ(scene.EntityCount(), 0U);

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
