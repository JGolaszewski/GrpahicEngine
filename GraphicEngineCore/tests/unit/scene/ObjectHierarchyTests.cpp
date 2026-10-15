#include "scene/include/Object.h"

#include <gtest/gtest.h>

#include <memory>

namespace
{
class CountingComponent final : public Engine::Components::Component
{
  public:
    void Update(float deltaTime) override
    {
        ++updates;
        lastDeltaTime = deltaTime;
    }

    int updates = 0;
    float lastDeltaTime = 0.0f;
};
} // namespace

TEST(ObjectHierarchyTests, RejectsDuplicateParentingAndCycles)
{
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

TEST(ObjectHierarchyTests, RejectsSelfAndAncestorOwnershipCycles)
{
    auto root = std::make_shared<Engine::Object>();
    auto child = std::make_shared<Engine::Object>();
    auto grandchild = std::make_shared<Engine::Object>();

    EXPECT_FALSE(root->AddChild(root));
    ASSERT_TRUE(root->AddChild(child));
    ASSERT_TRUE(child->AddChild(grandchild));
    EXPECT_FALSE(grandchild->AddChild(root));
    EXPECT_EQ(root->GetParent(), nullptr);
    EXPECT_EQ(grandchild->GetParent(), child.get());
}

TEST(ObjectHierarchyTests, DetachesChildAndClearsTransformParent)
{
    Engine::Object root;
    auto child = std::make_shared<Engine::Object>();
    ASSERT_TRUE(root.AddChild(child));
    ASSERT_EQ(child->transform.GetParent(), &root.transform);

    EXPECT_TRUE(root.RemoveChild(child.get()));
    EXPECT_EQ(child->GetParent(), nullptr);
    EXPECT_EQ(child->transform.GetParent(), nullptr);
    EXPECT_FALSE(root.RemoveChild(child.get()));
}

TEST(ObjectHierarchyTests, ReparentsThroughParentApiAndKeepsSingleOwnership)
{
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

TEST(ObjectHierarchyTests, SetParentHandlesNoOpSelfAndDetachCases)
{
    auto parent = std::make_shared<Engine::Object>();
    auto child = std::make_shared<Engine::Object>();

    EXPECT_FALSE(child->SetParent(nullptr));
    EXPECT_FALSE(child->SetParent(child.get()));
    ASSERT_TRUE(parent->AddChild(child));
    EXPECT_FALSE(child->SetParent(parent.get()));
    EXPECT_TRUE(child->SetParent(nullptr));
    EXPECT_EQ(child->GetParent(), nullptr);
    EXPECT_TRUE(parent->GetChildren().empty());

    Engine::Object stackOwned;
    EXPECT_FALSE(stackOwned.SetParent(nullptr));
    EXPECT_FALSE(stackOwned.SetParent(parent.get()));
}

TEST(ObjectHierarchyTests, UpdatesComponentsAndDescendantsRecursively)
{
    auto root = std::make_shared<Engine::Object>();
    auto child = std::make_shared<Engine::Object>();
    auto &rootComponent = root->AddComponent<CountingComponent>();
    auto &childComponent = child->AddComponent<CountingComponent>();
    ASSERT_TRUE(root->AddChild(child));

    root->Update(0.25f);

    EXPECT_EQ(rootComponent.updates, 1);
    EXPECT_EQ(rootComponent.lastDeltaTime, 0.25f);
    EXPECT_EQ(childComponent.updates, 1);
    EXPECT_EQ(childComponent.lastDeltaTime, 0.25f);
}
