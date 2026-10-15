#include "scene/include/Object.h"

#include <algorithm>
#include <ranges>

Engine::Object::~Object()
{
    DetachChildren();
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

auto Engine::Object::AddChild(const std::shared_ptr<Object> &child) -> bool
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
    return child->SetParent(this);
}

auto Engine::Object::RemoveChild(const Object *child) -> bool
{
    const auto found = std::ranges::find_if(
        m_Children, [child](const auto &candidate) -> bool { return candidate.get() == child; });
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
    if (parent == m_Parent)
    {
        return parent != this;
    }
    if (parent == this)
    {
        return false;
    }
    for (Object *ancestor = parent; ancestor != nullptr; ancestor = ancestor->m_Parent)
    {
        if (ancestor == this)
        {
            return false;
        }
    }
    std::shared_ptr<Object> self = weak_from_this().lock();
    if (parent != nullptr && !self)
    {
        return false;
    }
    if (parent != nullptr)
    {
        const bool alreadyPresent = std::ranges::any_of(
            parent->m_Children, [this](const auto &child) -> bool { return child.get() == this; });
        if (alreadyPresent)
        {
            return false;
        }
        parent->m_Children.push_back(self);
    }
    if (!transform.SetParent(parent == nullptr ? nullptr : &parent->transform))
    {
        if (parent != nullptr)
        {
            std::erase_if(parent->m_Children,
                          [this](const auto &sibling) -> bool { return sibling.get() == this; });
        }
        return false;
    }
    if (m_Parent != nullptr)
    {
        auto &siblings = m_Parent->m_Children;
        std::erase_if(siblings, [this](const auto &sibling) -> bool { return sibling.get() == this; });
    }
    m_Parent = parent;
    return true;
}

void Engine::Object::DetachChildren() noexcept
{
    for (const auto &child : m_Children)
    {
        child->m_Parent = nullptr;
        child->transform.SetParent(nullptr);
    }
    m_Children.clear();
}
