#include "scene/include/Object.h"

#include <gtest/gtest.h>

#include <memory>

TEST(ObjectHierarchyTests, RejectsDuplicateParentingAndCycles) {
  Engine::Object root;
  auto child = std::make_shared<Engine::Object>();
  auto grandchild = std::make_shared<Engine::Object>();

  ASSERT_TRUE(root.AddChild(child));
  ASSERT_TRUE(child->AddChild(grandchild));
  EXPECT_FALSE(root.AddChild(child));
  EXPECT_FALSE(child->AddChild(nullptr));
  EXPECT_FALSE(child->SetParent(grandchild.get()));
  EXPECT_EQ(child->GetParent(), &root);
  EXPECT_EQ(grandchild->GetParent(), child.get());
}

TEST(ObjectHierarchyTests, DetachesChildAndClearsTransformParent) {
  Engine::Object root;
  auto child = std::make_shared<Engine::Object>();
  ASSERT_TRUE(root.AddChild(child));
  ASSERT_EQ(child->transform.GetParent(), &root.transform);

  EXPECT_TRUE(root.RemoveChild(child.get()));
  EXPECT_EQ(child->GetParent(), nullptr);
  EXPECT_EQ(child->transform.GetParent(), nullptr);
  EXPECT_FALSE(root.RemoveChild(child.get()));
}

TEST(ObjectHierarchyTests, ReparentsThroughParentApiAndKeepsSingleOwnership) {
  Engine::Object first;
  Engine::Object second;
  auto child = std::make_shared<Engine::Object>();
  ASSERT_TRUE(first.AddChild(child));

  ASSERT_TRUE(child->SetParent(&second));
  EXPECT_TRUE(first.GetChildren().empty());
  ASSERT_EQ(second.GetChildren().size(), 1U);
  EXPECT_EQ(second.GetChildren().front().get(), child.get());
  EXPECT_EQ(child->transform.GetParent(), &second.transform);
}
