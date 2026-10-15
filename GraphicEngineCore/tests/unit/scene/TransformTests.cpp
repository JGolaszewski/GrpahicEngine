#include "scene/include/components/Transform.h"

#include <gtest/gtest.h>

#include <cmath>

TEST(TransformTests, ComposesLocalTranslationWithParentWorldTransform)
{
    Engine::Components::Transform parent;
    Engine::Components::Transform child;
    parent.SetPosition(3.0F, 2.0F, -1.0F);
    child.SetPosition(1.0F, 4.0F, 2.0F);
    child.SetParent(&parent);

    const DirectX::XMVECTOR origin = DirectX::XMVectorSet(0.0F, 0.0F, 0.0F, 1.0F);
    const DirectX::XMVECTOR world = DirectX::XMVector3TransformCoord(origin, child.GetWorldMat());
    DirectX::XMFLOAT3 position{};
    DirectX::XMStoreFloat3(&position, world);

    EXPECT_NEAR(position.x, 4.0F, 1.0e-5F);
    EXPECT_NEAR(position.y, 6.0F, 1.0e-5F);
    EXPECT_NEAR(position.z, 1.0F, 1.0e-5F);
}

TEST(TransformTests, RejectsAncestorCycleAndKeepsPreviousParent)
{
    Engine::Components::Transform root;
    Engine::Components::Transform child;
    Engine::Components::Transform grandchild;
    child.SetParent(&root);
    grandchild.SetParent(&child);

    grandchild.SetParent(&root);
    EXPECT_EQ(grandchild.GetParent(), &root);
    root.SetParent(&grandchild);
    EXPECT_EQ(root.GetParent(), nullptr);
}

TEST(TransformTests, SingularNormalMatrixRemainsFinite)
{
    Engine::Components::Transform transform;
    transform.SetScale(0.0F, 1.0F, 1.0F);
    const DirectX::XMMATRIX normal = transform.GetNormalMat();
    DirectX::XMFLOAT4X4 stored{};
    DirectX::XMStoreFloat4x4(&stored, normal);

    for (const float value : stored.m[0])
    {
        EXPECT_TRUE(std::isfinite(value));
    }
    for (const float value : stored.m[1])
    {
        EXPECT_TRUE(std::isfinite(value));
    }
    for (const float value : stored.m[2])
    {
        EXPECT_TRUE(std::isfinite(value));
    }
    for (const float value : stored.m[3])
    {
        EXPECT_TRUE(std::isfinite(value));
    }
}
