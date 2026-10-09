#pragma once

#include "Component.h"
#include "components/Transform.h"

#include <concepts>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine {
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251)
#endif
class ENGINE_API Object {
public:
  Object() = default;
  virtual ~Object() = default;
  Object(const Object&) = delete;
  auto operator=(const Object&) -> Object& = delete;
  Object(Object&&) = delete;
  auto operator=(Object&&) -> Object& = delete;

  auto AddChild(std::shared_ptr<Object> child) -> bool;
  auto RemoveChild(const Object* child) -> bool;
  auto GetChildren() const noexcept -> std::span<const std::shared_ptr<Object>> { return m_Children; }
  auto GetParent() const noexcept -> Object* { return m_Parent; }
  virtual void Update(float deltaTime);

  template <typename T, typename... Arguments>
    requires std::derived_from<T, Components::Component>
  auto AddComponent(Arguments&&... arguments) -> T& {
    auto component = std::make_unique<T>(std::forward<Arguments>(arguments)...);
    T& reference = *component;
    m_Components.push_back(std::move(component));
    return reference;
  }

  template <typename T>
    requires std::derived_from<T, Components::Component>
  auto GetComponent() noexcept -> T* {
    if constexpr (std::same_as<T, Components::Transform>) {
      return &transform;
    }
    for (const auto& component : m_Components) {
      if (auto* match = dynamic_cast<T*>(component.get())) {
        return match;
      }
    }
    return nullptr;
  }

  Components::Transform transform;

private:
  Object* m_Parent = nullptr;
  std::vector<std::shared_ptr<Object>> m_Children;
  std::vector<std::unique_ptr<Components::Component>> m_Components;
};
#ifdef _MSC_VER
#  pragma warning(pop)
#endif
} // namespace Engine
