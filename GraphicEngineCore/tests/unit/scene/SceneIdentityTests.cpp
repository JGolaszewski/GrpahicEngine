#include "rendering/include/renderer/Entity.h"
#include "scene/include/Scene.h"

#include <gtest/gtest.h>

#include <memory>

TEST(SceneIdentityTests, RestoresIdsAndKeepsIndexConsistentAfterRemovalAndReplacement) {
  Engine::Scene scene;
  auto first = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
  auto middle = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
  auto last = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
  ASSERT_EQ(scene.AddEntity(first, 10), 10U);
  ASSERT_EQ(scene.AddEntity(middle, 20), 20U);
  ASSERT_EQ(scene.AddEntity(last, 30), 30U);

  scene.RemoveEntity(middle.get());
  EXPECT_EQ(scene.FindEntity(10), first);
  EXPECT_EQ(scene.FindEntity(20), nullptr);
  EXPECT_EQ(scene.FindEntity(30), last);
  EXPECT_EQ(scene.GetEntityId(last.get()), 30U);

  auto replacement = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
  ASSERT_TRUE(scene.ReplaceEntity(30, replacement));
  EXPECT_EQ(scene.FindEntity(30), replacement);
  EXPECT_EQ(scene.GetEntityId(replacement.get()), 30U);
  EXPECT_EQ(scene.AddEntity(std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr)), 31U);
  EXPECT_EQ(scene.AddEntity(std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr), 10), Engine::InvalidEntityId);
}

TEST(SceneIdentityTests, InvalidIndexReturnsInvalidId) {
  Engine::Scene scene;
  EXPECT_EQ(scene.GetEntityIdAt(0), Engine::InvalidEntityId);
}
