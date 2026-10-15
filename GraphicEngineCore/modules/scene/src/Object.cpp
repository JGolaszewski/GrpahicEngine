#include "scene/include/Object.h"

#include <algorithm>
#include <DirectXMath.h>
#include <cmath>
#include <ranges>

Engine::Object::~Object()
{
    for (const auto &child : m_Children)
    {
        child->m_Parent = nullptr;
        child->transform.SetParent(nullptr);
    }
}

void Engine::Object::Update(float deltaTime)
{
    for (const auto &component : m_Components)
    {
        component->Update(deltaTime);
    }
    for (const auto &child : m_Children)
    {
        child->Update(deltaTime);
    }
}

auto Engine::Object::AddChild(std::shared_ptr<Object> child) -> bool
{
    if (!child || child.get() == this || child->m_Parent != nullptr)
    {
        return false;
    }
    for (const Object *ancestor = this; ancestor != nullptr; ancestor = ancestor->m_Parent)
    {
        if (ancestor == child.get())
        {
            return false;
        }
    }
    child->m_Parent = this;
    child->transform.SetParent(&transform);
    m_Children.push_back(std::move(child));
    return true;
}

auto Engine::Object::SetParent(Object *parent, ParentTransformMode mode) -> bool
{
    if (parent == m_Parent)
    {
        return false;
    }
    if (parent == this)
    {
        return false;
    }
    for (const Object *ancestor = parent; ancestor != nullptr; ancestor = ancestor->m_Parent)
    {
        if (ancestor == this)
        {
            return false;
        }
    }

    DirectX::XMFLOAT3 localPosition = transform.Position;
    DirectX::XMFLOAT4 localRotation = transform.Rotation;
    DirectX::XMFLOAT3 localScale = transform.Scale;
    if (mode == ParentTransformMode::KeepWorld)
    {
        using namespace DirectX;
        XMMATRIX localMatrix = transform.GetWorldMat();
        if (parent != nullptr)
        {
            XMVECTOR determinant;
            const XMMATRIX inverseParent = XMMatrixInverse(&determinant, parent->transform.GetWorldMat());
            const float determinantValue = XMVectorGetX(determinant);
            if (!std::isfinite(determinantValue) || std::abs(determinantValue) < 1.0e-8f)
            {
                return false;
            }
            localMatrix = XMMatrixMultiply(localMatrix, inverseParent);
        }
        XMVECTOR scale;
        XMVECTOR rotation;
        XMVECTOR translation;
        if (!XMMatrixDecompose(&scale, &rotation, &translation, localMatrix))
        {
            return false;
        }
        XMStoreFloat3(&localPosition, translation);
        XMStoreFloat4(&localRotation, rotation);
        XMStoreFloat3(&localScale, scale);
    }

    std::shared_ptr<Object> self;
    if (parent != nullptr)
    {
        self = weak_from_this().lock();
        if (!self)
        {
            return false;
        }
    }
    if (parent != nullptr)
    {
        const auto duplicate = std::ranges::find_if(
            parent->m_Children, [this](const auto &candidate) -> bool { return candidate.get() == this; });
        if (duplicate != parent->m_Children.end())
        {
            return false;
        }
        parent->m_Children.reserve(parent->m_Children.size() + 1);
    }

    if (m_Parent != nullptr)
    {
        const auto child = std::ranges::find_if(
            m_Parent->m_Children, [this](const auto &candidate) -> bool { return candidate.get() == this; });
        if (child == m_Parent->m_Children.end())
        {
            return false;
        }
        self = std::move(*child);
        m_Parent->m_Children.erase(child);
    }

    m_Parent = parent;
    transform.SetParent(parent != nullptr ? &parent->transform : nullptr);
    if (parent != nullptr)
    {
        parent->m_Children.push_back(std::move(self));
    }
    if (mode == ParentTransformMode::KeepWorld)
    {
        transform.SetPosition(localPosition.x, localPosition.y, localPosition.z);
        transform.Rotation = localRotation;
        transform.SetScale(localScale.x, localScale.y, localScale.z);
    }
    return true;
}

auto Engine::Object::RemoveChild(const Object *child) -> bool
{
    const auto found =
        std::ranges::find_if(m_Children, [child](const auto &candidate) { return candidate.get() == child; });
    if (found == m_Children.end())
    {
        return false;
    }
    (*found)->m_Parent = nullptr;
    (*found)->transform.SetParent(nullptr);
    m_Children.erase(found);
    return true;
}
