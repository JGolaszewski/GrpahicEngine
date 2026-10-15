#pragma once

#include "Component.h"
#include "components/Transform.h"

#include <concepts>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif
/// Hierarchical scene object that owns children, components, and a transform.
class ENGINE_API Object : public std::enable_shared_from_this<Object>
{
  public:
    /// Creates an unparented object with no children or components.
    Object() = default;

    /// Releases owned children and components.
    virtual ~Object() = default;

    /// Objects have unique ownership identity and cannot be copied.
    Object(const Object &) = delete;
    auto operator=(const Object &) -> Object & = delete;

    /// Objects have stable parent and component relationships and cannot be moved.
    Object(Object &&) = delete;
    auto operator=(Object &&) -> Object & = delete;

    /// Adds a child while preserving single-parent ownership and rejecting cycles.
    /// @param child Child object to attach.
    /// @return True when the child was attached.
    auto AddChild(std::shared_ptr<Object> child) -> bool;

    /// Detaches a direct child from this object.
    /// @param child Child object to remove.
    /// @return True when the object was a direct child.
    auto RemoveChild(const Object *child) -> bool;

    /// Reparents this object while rejecting cycles.
    /// @param parent New parent, or null to detach.
    /// @return True when the parent relationship changed successfully.
    auto SetParent(Object *parent) -> bool;

    /// Returns the owned direct children.
    /// @return Read-only span of child shared pointers.
    auto GetChildren() const noexcept -> std::span<const std::shared_ptr<Object>>
    {
        return m_Children;
    }
    /// Returns the current parent.
    /// @return Borrowed parent pointer, or null when unparented.
    auto GetParent() const noexcept -> Object *
    {
        return m_Parent;
    }
    /// Updates this object and its components and children.
    /// @param deltaTime Elapsed frame time in seconds.
    virtual void Update(float deltaTime);

    /// Constructs and attaches a component of the requested derived type.
    /// @tparam T Component type derived from Components::Component.
    /// @tparam Arguments Constructor argument types.
    /// @param arguments Arguments forwarded to the component constructor.
    /// @return Reference to the newly owned component.
    template <typename T, typename... Arguments>
        requires std::derived_from<T, Components::Component>
    auto AddComponent(Arguments &&...arguments) -> T &
    {
        auto component = std::make_unique<T>(std::forward<Arguments>(arguments)...);
        T &reference = *component;
        m_Components.push_back(std::move(component));
        return reference;
    }

    /// Finds the first component of the requested derived type.
    /// Transforms are stored as the dedicated public transform member.
    /// @tparam T Component type derived from Components::Component.
    /// @return Borrowed component pointer, or null when none is attached.
    template <typename T>
        requires std::derived_from<T, Components::Component>
    auto GetComponent() noexcept -> T *
    {
        if constexpr (std::same_as<T, Components::Transform>)
        {
            return &transform;
        }
        for (const auto &component : m_Components)
        {
            if (auto *match = dynamic_cast<T *>(component.get()))
            {
                return match;
            }
        }
        return nullptr;
    }

    Components::Transform transform; ///< Local transform composed with the object's parent hierarchy.

  private:
    Object *m_Parent = nullptr;
    std::vector<std::shared_ptr<Object>> m_Children;
    std::vector<std::unique_ptr<Components::Component>> m_Components;
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
} // namespace Engine
