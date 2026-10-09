#include "scene/include/Object.h"

#include <algorithm>
#include <ranges>

void Engine::Object::Update(float deltaTime) {
  for (const auto& component : m_Components) {
    component->Update(deltaTime);
  }
  for (const auto& child : m_Children) {
    child->Update(deltaTime);
  }
}

auto Engine::Object::AddChild(std::shared_ptr<Object> child) -> bool {
  if (!child || child.get() == this || child->m_Parent != nullptr) {
    return false;
  }
  for (const Object* ancestor = this; ancestor != nullptr; ancestor = ancestor->m_Parent) {
    if (ancestor == child.get()) {
      return false;
    }
  }
  child->m_Parent = this;
  child->transform.SetParent(&transform);
  m_Children.push_back(std::move(child));
  return true;
}

auto Engine::Object::RemoveChild(const Object* child) -> bool {
  const auto found =
      std::ranges::find_if(m_Children, [child](const auto& candidate) { return candidate.get() == child; });
  if (found == m_Children.end()) {
    return false;
  }
  (*found)->m_Parent = nullptr;
  (*found)->transform.SetParent(nullptr);
  m_Children.erase(found);
  return true;
}
