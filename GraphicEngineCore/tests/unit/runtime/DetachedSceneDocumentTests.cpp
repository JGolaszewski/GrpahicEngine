#include "runtime/include/Application.h"
#include "scene/include/Scene.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class SceneReplacementTrackingApplication final : public Engine::Application {
public:
  uint32_t ReplacementCount = 0;

protected:
  void OnSceneReplaced(const std::shared_ptr<Engine::Scene>&) override { ++ReplacementCount; }
};

auto MakeSimpleDocument(uint64_t entityId, std::string name) -> Engine::SceneDocument {
  Engine::SceneDocument document;
  Engine::SceneEntityDocument entity;
  entity.id = entityId;
  entity.name = std::move(name);
  document.entities.push_back(std::move(entity));
  return document;
}

TEST(DetachedSceneDocumentTests, CreatesIndependentSceneWithoutChangingActiveScenes) {
  SceneReplacementTrackingApplication application;
  auto authoredScene = std::make_shared<Engine::Scene>();
  application.AddScene(authoredScene);
  const std::vector<std::shared_ptr<Engine::Scene>> activeScenesBefore = application.GetScenes();
  const size_t authoredEntityCount = authoredScene->EntityCount();
  std::vector<std::string> warnings;
  warnings.push_back("old warning");

  auto first = application.CreateDetachedSceneFromDocument(MakeSimpleDocument(1, "First"), warnings);
  ASSERT_TRUE(first.has_value());
  EXPECT_TRUE(warnings.empty());
  EXPECT_EQ(application.GetScenes(), activeScenesBefore);
  EXPECT_EQ(application.GetScenes().front(), authoredScene);
  EXPECT_EQ(application.ReplacementCount, 0U);

  auto second = application.CreateDetachedSceneFromDocument(MakeSimpleDocument(2, "Second"), warnings);
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(*first, *second);
  EXPECT_NE(*first, authoredScene);

  const auto firstEntityCount = (*first)->EntityCount();
  (void)(*first)->QueueCreate("Only in first variant");
  (void)(*first)->sync();
  EXPECT_EQ((*first)->EntityCount(), firstEntityCount + 1);
  EXPECT_EQ((*second)->EntityCount(), firstEntityCount);
  EXPECT_EQ(authoredScene->EntityCount(), authoredEntityCount);
  EXPECT_EQ(application.GetScenes(), activeScenesBefore);
}

TEST(DetachedSceneDocumentTests, FailedInstantiationDoesNotReplaceOrMutateActiveScene) {
  SceneReplacementTrackingApplication application;
  auto authoredScene = std::make_shared<Engine::Scene>();
  application.AddScene(authoredScene);
  const std::vector<std::shared_ptr<Engine::Scene>> activeScenesBefore = application.GetScenes();
  Engine::SceneDocument invalidDocument;
  Engine::SceneLightDocument invalidLight;
  invalidLight.id = 9;
  invalidLight.name = "Invalid light";
  invalidLight.type = "unsupported";
  invalidDocument.lights.push_back(invalidLight);
  std::vector<std::string> warnings;

  auto detached = application.CreateDetachedSceneFromDocument(invalidDocument, warnings);
  ASSERT_FALSE(detached.has_value());
  EXPECT_FALSE(detached.error().Message.empty());
  EXPECT_EQ(application.GetScenes(), activeScenesBefore);
  EXPECT_EQ(application.GetScenes().front(), authoredScene);
  EXPECT_EQ(authoredScene->EntityCount(), 1U);
  EXPECT_EQ(application.ReplacementCount, 0U);
}

} // namespace
