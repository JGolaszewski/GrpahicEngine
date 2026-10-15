#include "scene/include/Object.h"

#include <algorithm>
#include <ranges>

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

auto Engine::Object::SetParent(Object *parent) -> bool
{
    if (parent == m_Parent || parent == this)
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
    if (parent == nullptr)
    {
        return m_Parent != nullptr && m_Parent->RemoveChild(this);
    }

    auto self = weak_from_this().lock();
    if (!self)
    {
        return false;
    }
    parent->m_Children.reserve(parent->m_Children.size() + 1);
    auto *oldParent = m_Parent;
    if (oldParent != nullptr)
    {
        const auto found = std::ranges::find_if(
            oldParent->m_Children, [this](const auto &candidate) { return candidate.get() == this; });
        if (found == oldParent->m_Children.end())
        {
            return false;
        }
        oldParent->m_Children.erase(found);
    }
    m_Parent = parent;
    transform.SetParent(&parent->transform);
    parent->m_Children.push_back(std::move(self));
    return true;
}
