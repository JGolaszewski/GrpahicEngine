#include "scene/include/Scene.h"

#include <algorithm>
#include <atomic>
#include <random>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Engine {
namespace {
auto GenerateSceneGuid() -> Ecs::SceneGuid {
  static const uint64_t nameSpace = [] -> uint64_t {
    std::random_device random;
    return (static_cast<uint64_t>(random()) << 32U) | static_cast<uint64_t>(random());
  }();
  static std::atomic<uint64_t> nextGuid{1};
  return {.High = nameSpace, .Low = nextGuid.fetch_add(1, std::memory_order_relaxed)};
}

auto HandleKey(EntityHandle handle) noexcept -> uint64_t {
  return (static_cast<uint64_t>(handle.Index) << 32U) | handle.Generation;
}
} // namespace

Scene::Scene() {
  auto root = CreateImmediate("Scene", InvalidEntityHandle);
  if (!root) {
    throw std::runtime_error(root.error().Message);
  }
  m_SceneRoot = *root;
  const Status marked = m_World.Add<Ecs::SceneRoot>(m_SceneRoot, Ecs::SceneRoot{});
  if (!marked) {
    throw std::runtime_error(marked.error().Message);
  }
}

auto Scene::QueueCommand(EntityHandle entity, Command command) -> uint64_t {
  std::scoped_lock lock(m_CommandMutex);
  const uint64_t sequence = m_NextSequence++;
  m_Commands.push_back(
      {.Sequence = sequence, .SceneEpoch = m_SceneEpoch, .Entity = entity, .Apply = std::move(command)});
  return sequence;
}

auto Scene::QueueCreate(std::string name, EntityHandle parent, EntityPreset preset) -> uint64_t {
  return QueueCommand(InvalidEntityHandle,
                      [name = std::move(name), parent, preset](Scene& scene, EntityHandle& result) -> Status {
                        const EntityHandle actualParent = parent == InvalidEntityHandle ? scene.m_SceneRoot : parent;
                        auto created = scene.CreateImmediate(name, actualParent);
                        if (!created) {
                          return {std::unexpected(std::move(created.error()))};
                        }
                        result = *created;
                        Status configured;
                        switch (preset) {
                        case EntityPreset::Empty:
                          break;
                        case EntityPreset::Mesh:
                          configured = scene.m_World.Add<Ecs::MeshRenderer>(result, Ecs::MeshRenderer{});
                          break;
                        case EntityPreset::SpotLight:
                          configured = scene.m_World.Add<Ecs::SpotLight>(result, Ecs::SpotLight{});
                          break;
                        case EntityPreset::Camera:
                          configured = scene.m_World.Add<Ecs::Camera>(result, Ecs::Camera{});
                          break;
                        }
                        return configured;
                      });
}

auto Scene::QueueDestroy(EntityHandle entity) -> uint64_t {
  return QueueCommand(entity,
                      [entity](Scene& scene, EntityHandle&) -> Status { return scene.DestroyImmediate(entity); });
}

auto Scene::QueueReparent(EntityHandle entity, EntityHandle parent) -> uint64_t {
  return QueueCommand(entity, [entity, parent](Scene& scene, EntityHandle&) -> Status {
    return scene.ReparentImmediate(entity, parent);
  });
}

auto Scene::sync() -> std::vector<SyncResult> {
  std::vector<SyncResult> results;
  std::vector<QueuedCommand> batch;
  {
    std::scoped_lock lock(m_CommandMutex);
    if (m_Syncing) {
      return {};
    }
    m_Syncing = true;
    batch.swap(m_Commands);
  }

  while (true) {
    std::ranges::sort(batch, {}, &QueuedCommand::Sequence);
    for (auto& command : batch) {
      SyncResult result{.Sequence = command.Sequence, .Entity = command.Entity};
      if (command.SceneEpoch != m_SceneEpoch) {
        result.Outcome = std::unexpected(MakeError("Structural command belongs to an obsolete scene epoch"));
      } else {
        try {
          result.Outcome = command.Apply(*this, result.Entity);
          result.EntityCreated = command.Entity == InvalidEntityHandle && result.Outcome.has_value() &&
                                 result.Entity != InvalidEntityHandle;
        } catch (const std::exception& error) {
          result.Outcome = std::unexpected(MakeError(error.what()));
        } catch (...) {
          result.Outcome = std::unexpected(MakeError("Structural command failed"));
        }
      }
      results.push_back(std::move(result));
    }
    batch.clear();

    std::scoped_lock lock(m_CommandMutex);
    if (m_Commands.empty()) {
      m_Syncing = false;
      break;
    }
    batch.swap(m_Commands);
  }

  if (m_CachedTopologyRevision != m_TopologyRevision) {
    const Status rebuilt = RebuildHierarchyIndex();
    if (!rebuilt) {
      results.push_back({.Outcome = std::unexpected(rebuilt.error())});
    }
  }
  return results;
}

auto Scene::CreateImmediate(const std::string& name, EntityHandle parent) -> Result<EntityHandle> {
  if (parent != InvalidEntityHandle && !m_World.IsAlive(parent)) {
    return std::unexpected(MakeError("Cannot create an entity below a stale parent"));
  }

  const Ecs::Name nameComponent{.Value = name};
  const Ecs::Parent parentComponent{.Id = InvalidEntityHandle};
  const Ecs::ChildList childLinks{};
  const Ecs::LocalTransform localTransform;
  const Ecs::WorldTransform worldTransform;
  const Ecs::LocalVisibility localVisibility{};
  const Ecs::GlobalVisibility globalVisibility{};
  const Ecs::PersistentIdentity identityComponent{.Guid = GenerateSceneGuid()};
  auto created = m_World.Create(nameComponent, identityComponent, parentComponent, childLinks, localTransform,
                                worldTransform, localVisibility, globalVisibility);
  if (!created) {
    return std::unexpected(std::move(created.error()));
  }
  const Status attached = AttachChild(parent, *created);
  if (!attached) {
    const Status destroyed = m_World.Destroy(*created);
    return destroyed ? std::unexpected(attached.error()) : std::unexpected(destroyed.error());
  }
  ++m_TopologyRevision;
  return *created;
}

auto Scene::AttachChild(EntityHandle parent, EntityHandle child) -> Status {
  auto* childParent = m_World.Get<Ecs::Parent>(child);
  auto* childLinks = m_World.Get<Ecs::ChildList>(child);
  if (childParent == nullptr || childLinks == nullptr || childParent->Id != InvalidEntityHandle ||
      childLinks->PreviousSibling != InvalidEntityHandle || childLinks->NextSibling != InvalidEntityHandle) {
    return std::unexpected(MakeError("Cannot attach an invalid or already-linked child"));
  }
  if (parent == InvalidEntityHandle) {
    return {};
  }

  auto* parentLinks = m_World.Get<Ecs::ChildList>(parent);
  if (parentLinks == nullptr) {
    return std::unexpected(MakeError("Cannot attach a child below a stale or invalid parent"));
  }
  if (parentLinks->LastChild == InvalidEntityHandle) {
    if (parentLinks->FirstChild != InvalidEntityHandle) {
      return std::unexpected(MakeError("Hierarchy child-list head/tail invariant is broken"));
    }
    parentLinks->FirstChild = child;
  } else {
    auto* tailLinks = m_World.Get<Ecs::ChildList>(parentLinks->LastChild);
    auto* tailParent = m_World.Get<Ecs::Parent>(parentLinks->LastChild);
    if (tailLinks == nullptr || tailParent == nullptr || tailParent->Id != parent ||
        tailLinks->NextSibling != InvalidEntityHandle) {
      return std::unexpected(MakeError("Hierarchy contains an invalid last child"));
    }
    tailLinks->NextSibling = child;
    childLinks->PreviousSibling = parentLinks->LastChild;
  }
  childParent->Id = parent;
  childLinks->NextSibling = InvalidEntityHandle;
  parentLinks->LastChild = child;
  return {};
}

auto Scene::Detach(EntityHandle entity) -> Status {
  auto* parent = m_World.Get<Ecs::Parent>(entity);
  auto* links = m_World.Get<Ecs::ChildList>(entity);
  if (parent == nullptr || links == nullptr) {
    return std::unexpected(MakeError("Cannot detach a stale hierarchy entity"));
  }

  Ecs::ChildList* parentLinks = parent->Id == InvalidEntityHandle ? nullptr : m_World.Get<Ecs::ChildList>(parent->Id);
  auto* previous =
      links->PreviousSibling == InvalidEntityHandle ? nullptr : m_World.Get<Ecs::ChildList>(links->PreviousSibling);
  auto* next = links->NextSibling == InvalidEntityHandle ? nullptr : m_World.Get<Ecs::ChildList>(links->NextSibling);
  if (links->PreviousSibling != InvalidEntityHandle && previous == nullptr) {
    return std::unexpected(MakeError("Hierarchy contains a stale previous sibling"));
  }
  if (links->NextSibling != InvalidEntityHandle && next == nullptr) {
    return std::unexpected(MakeError("Hierarchy contains a stale next sibling"));
  }

  if (parentLinks != nullptr) {
    if (parentLinks->FirstChild == entity) {
      parentLinks->FirstChild = links->NextSibling;
    }
    if (parentLinks->LastChild == entity) {
      parentLinks->LastChild = links->PreviousSibling;
    }
  }
  if (previous != nullptr) {
    previous->NextSibling = links->NextSibling;
  }
  if (next != nullptr) {
    next->PreviousSibling = links->PreviousSibling;
  }
  parent->Id = InvalidEntityHandle;
  links->PreviousSibling = InvalidEntityHandle;
  links->NextSibling = InvalidEntityHandle;
  return {};
}

auto Scene::ReparentImmediate(EntityHandle entity, EntityHandle parent) -> Status {
  if (entity == m_SceneRoot) {
    return std::unexpected(MakeError("The scene root cannot be reparented"));
  }
  if (parent == InvalidEntityHandle) {
    parent = m_SceneRoot;
  }
  auto* hierarchyParent = m_World.Get<Ecs::Parent>(entity);
  auto* entityLinks = m_World.Get<Ecs::ChildList>(entity);
  if (hierarchyParent == nullptr || entityLinks == nullptr) {
    return {}; // stale handles are harmless no-ops for queued structural requests
  }
  if (entity == parent) {
    return std::unexpected(MakeError("An entity cannot be its own parent"));
  }
  if (hierarchyParent->Id == parent) {
    return {};
  }
  if (parent != InvalidEntityHandle && m_World.Get<Ecs::ChildList>(parent) == nullptr) {
    return std::unexpected(MakeError("Cannot reparent to a stale or invalid parent"));
  }
  for (EntityHandle ancestor = parent; ancestor != InvalidEntityHandle;) {
    if (ancestor == entity) {
      return std::unexpected(MakeError("Reparenting would create a hierarchy cycle"));
    }
    const auto* ancestorParent = m_World.Get<Ecs::Parent>(ancestor);
    if (ancestorParent == nullptr) {
      return std::unexpected(MakeError("Hierarchy contains a stale ancestor handle"));
    }
    ancestor = ancestorParent->Id;
  }

  // Validate the new tail before detaching, so a malformed target cannot leave
  // the entity detached after a failed operation.
  if (parent != InvalidEntityHandle) {
    const auto* target = m_World.Get<Ecs::ChildList>(parent);
    if (target == nullptr ||
        (target->LastChild == InvalidEntityHandle) != (target->FirstChild == InvalidEntityHandle)) {
      return std::unexpected(MakeError("Destination child-list invariant is broken"));
    }
    if (target->LastChild != InvalidEntityHandle) {
      const auto* tail = m_World.Get<Ecs::ChildList>(target->LastChild);
      const auto* tailParent = m_World.Get<Ecs::Parent>(target->LastChild);
      if (tail == nullptr || tailParent == nullptr || tailParent->Id != parent ||
          tail->NextSibling != InvalidEntityHandle) {
        return std::unexpected(MakeError("Destination has an invalid last child"));
      }
    }
  }
  const Status detached = Detach(entity);
  if (!detached) {
    return detached;
  }
  const Status attached = AttachChild(parent, entity);
  if (!attached) {
    return attached;
  }
  if (auto* local = m_World.Get<Ecs::LocalTransform>(entity); local != nullptr) {
    local->IsDirty = true;
  }
  ++m_TopologyRevision;
  return {};
}

auto Scene::DestroyImmediate(EntityHandle entity) -> Status {
  if (entity == m_SceneRoot) {
    return std::unexpected(MakeError("The scene root cannot be deleted"));
  }
  auto* links = m_World.Get<Ecs::ChildList>(entity);
  auto* parent = m_World.Get<Ecs::Parent>(entity);
  if (links == nullptr || parent == nullptr) {
    return {}; // stale/duplicate destroy is an idempotent no-op
  }

  std::vector<EntityHandle> children;
  std::unordered_set<uint64_t> visited;
  EntityHandle previous = InvalidEntityHandle;
  for (EntityHandle child = links->FirstChild; child != InvalidEntityHandle;) {
    if (!visited.insert(HandleKey(child)).second) {
      return std::unexpected(MakeError("Hierarchy child list contains a sibling cycle"));
    }
    const auto* childParent = m_World.Get<Ecs::Parent>(child);
    const auto* childLinks = m_World.Get<Ecs::ChildList>(child);
    if (childParent == nullptr || childLinks == nullptr || childParent->Id != entity ||
        childLinks->PreviousSibling != previous) {
      return std::unexpected(MakeError("Hierarchy contains an invalid child link"));
    }
    children.push_back(child);
    previous = child;
    child = childLinks->NextSibling;
  }
  if (previous != links->LastChild ||
      ((links->FirstChild == InvalidEntityHandle) != (links->LastChild == InvalidEntityHandle))) {
    return std::unexpected(MakeError("Hierarchy child-list head/tail invariant is broken"));
  }

  for (EntityHandle child : children) {
    (void)QueueDestroy(child);
  }
  const Status detached = Detach(entity);
  if (!detached) {
    return detached;
  }
  const Status destroyed = m_World.Destroy(entity);
  if (destroyed) {
    ++m_TopologyRevision;
  }
  return destroyed;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto Scene::RebuildHierarchyIndex() -> Status {
  std::vector<HierarchyWorkItem> order;
  std::vector<EntityHandle> roots;
  m_World.ForEachChunkReadOnly<Ecs::Parent, Ecs::ChildList>([&roots](std::span<const EntityHandle> entities,
                                                                     std::span<const Ecs::Parent> parents,
                                                                     std::span<const Ecs::ChildList> links) -> void {
    for (size_t row = 0; row < entities.size(); ++row) {
      if (parents[row].Id == InvalidEntityHandle) {
        if (links[row].PreviousSibling != InvalidEntityHandle || links[row].NextSibling != InvalidEntityHandle) {
          continue;
        }
        roots.push_back(entities[row]);
      }
    }
  });
  std::ranges::sort(roots, [](EntityHandle left, EntityHandle right) -> bool { return left.Index < right.Index; });

  struct Pending {
    EntityHandle Entity;
    uint32_t Depth;
    uint32_t ParentIndex;
  };
  std::vector<Pending> pending;
  for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
    pending.push_back({.Entity = *it, .Depth = 0, .ParentIndex = UINT32_MAX});
  }
  std::unordered_set<uint64_t> visited;
  visited.reserve(m_World.EntityCount());
  order.reserve(m_World.EntityCount());

  while (!pending.empty()) {
    const Pending item = pending.back();
    pending.pop_back();
    if (!m_World.IsAlive(item.Entity) || !visited.insert(HandleKey(item.Entity)).second) {
      return std::unexpected(MakeError("Hierarchy contains a stale handle, duplicate, or cycle"));
    }
    const auto* parent = m_World.Get<Ecs::Parent>(item.Entity);
    const auto* links = m_World.Get<Ecs::ChildList>(item.Entity);
    if (parent == nullptr || links == nullptr ||
        (item.ParentIndex == UINT32_MAX ? parent->Id != InvalidEntityHandle
                                        : parent->Id != order[item.ParentIndex].Entity)) {
      return std::unexpected(MakeError("Hierarchy parent/child links are inconsistent"));
    }
    const uint32_t workIndex = static_cast<uint32_t>(order.size());
    order.push_back({.Entity = item.Entity, .Depth = item.Depth, .ParentWorkIndex = item.ParentIndex});

    std::vector<EntityHandle> children;
    EntityHandle previous = InvalidEntityHandle;
    for (EntityHandle child = links->FirstChild; child != InvalidEntityHandle;) {
      const auto* childParent = m_World.Get<Ecs::Parent>(child);
      const auto* childLinks = m_World.Get<Ecs::ChildList>(child);
      if (childParent == nullptr || childLinks == nullptr || childParent->Id != item.Entity ||
          childLinks->PreviousSibling != previous) {
        return std::unexpected(MakeError("Hierarchy contains a stale or inconsistent child link"));
      }
      children.push_back(child);
      previous = child;
      child = childLinks->NextSibling;
    }
    if (previous != links->LastChild ||
        ((links->FirstChild == InvalidEntityHandle) != (links->LastChild == InvalidEntityHandle))) {
      return std::unexpected(MakeError("Hierarchy child-list head/tail invariant is broken"));
    }
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      pending.push_back({.Entity = *it, .Depth = item.Depth + 1, .ParentIndex = workIndex});
    }
  }
  if (order.size() != m_World.EntityCount()) {
    return std::unexpected(MakeError("Hierarchy has entities that are unreachable from its roots"));
  }

  uint32_t maxDepth = 0;
  for (const auto& item : order) {
    maxDepth = (std::max)(maxDepth, item.Depth);
  }
  std::vector<size_t> offsets(static_cast<size_t>(maxDepth) + 2, 0);
  for (const auto& item : order) {
    ++offsets[static_cast<size_t>(item.Depth) + 1];
  }
  for (size_t depth = 1; depth < offsets.size(); ++depth) {
    offsets[depth] += offsets[depth - 1];
  }
  std::vector<uint32_t> levelOrder(order.size());
  std::vector<size_t> cursor = offsets;
  for (uint32_t index = 0; index < order.size(); ++index) {
    levelOrder[cursor[order[index].Depth]++] = index;
  }
  m_HierarchyOrder = std::move(order);
  m_LevelOrder = std::move(levelOrder);
  m_LevelOffsets = std::move(offsets);
  m_RecomputedThisUpdate.assign(m_HierarchyOrder.size(), 0);
  m_CachedTopologyRevision = m_TopologyRevision;
  return {};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto Scene::UpdateDerivedState() -> Status {
  if (m_CachedTopologyRevision != m_TopologyRevision) {
    const Status rebuilt = RebuildHierarchyIndex();
    if (!rebuilt) {
      return rebuilt;
    }
  }
  std::ranges::fill(m_RecomputedThisUpdate, uint8_t{0});
  for (size_t depth = 0; depth + 1 < m_LevelOffsets.size(); ++depth) {
    for (size_t cursor = m_LevelOffsets[depth]; cursor < m_LevelOffsets[depth + 1]; ++cursor) {
      const uint32_t index = m_LevelOrder[cursor];
      const auto& item = m_HierarchyOrder[index];
      auto* local = m_World.Get<Ecs::LocalTransform>(item.Entity);
      auto* world = m_World.Get<Ecs::WorldTransform>(item.Entity);
      if (local == nullptr || world == nullptr) {
        return std::unexpected(MakeError("Entity is missing transform data"));
      }
      const bool hasParent = item.ParentWorkIndex != UINT32_MAX;
      const bool shouldRecompute = local->IsDirty || (hasParent && m_RecomputedThisUpdate[item.ParentWorkIndex] != 0);
      if (!shouldRecompute) {
        continue;
      }
      const DirectX::XMFLOAT3 scaleValue{local->Scale[0], local->Scale[1], local->Scale[2]};
      const DirectX::XMFLOAT4 rotationValue{local->Rotation[0], local->Rotation[1], local->Rotation[2],
                                            local->Rotation[3]};
      const DirectX::XMFLOAT3 translationValue{local->Position[0], local->Position[1], local->Position[2]};
      const DirectX::XMVECTOR scale = DirectX::XMLoadFloat3(&scaleValue);
      const DirectX::XMVECTOR rotation = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&rotationValue));
      const DirectX::XMVECTOR translation = DirectX::XMLoadFloat3(&translationValue);
      const DirectX::XMMATRIX localMatrix =
          DirectX::XMMatrixAffineTransformation(scale, DirectX::XMVectorZero(), rotation, translation);
      DirectX::XMMATRIX result = localMatrix;
      if (hasParent) {
        const auto* parentWorld = m_World.Get<Ecs::WorldTransform>(m_HierarchyOrder[item.ParentWorkIndex].Entity);
        if (parentWorld == nullptr) {
          return std::unexpected(MakeError("Parent is missing world transform data"));
        }
        result = localMatrix * DirectX::XMLoadFloat4x4(&parentWorld->Matrix);
      }
      DirectX::XMStoreFloat4x4(&world->Matrix, result);
      local->IsDirty = false;
      m_RecomputedThisUpdate[index] = 1;
    }
  }

  for (const auto& item : m_HierarchyOrder) {
    auto* global = m_World.Get<Ecs::GlobalVisibility>(item.Entity);
    const auto* local = m_World.Get<Ecs::LocalVisibility>(item.Entity);
    if (global == nullptr || local == nullptr) {
      return std::unexpected(MakeError("Entity is missing visibility data"));
    }
    if (item.ParentWorkIndex == UINT32_MAX) {
      global->IsVisible = local->IsVisible;
    } else {
      const auto* parentGlobal = m_World.Get<Ecs::GlobalVisibility>(m_HierarchyOrder[item.ParentWorkIndex].Entity);
      if (parentGlobal == nullptr) {
        return std::unexpected(MakeError("Parent is missing global visibility data"));
      }
      global->IsVisible = local->IsVisible && parentGlobal->IsVisible;
    }
  }
  return {};
}

auto Scene::HierarchySnapshot() const -> std::vector<EntitySnapshot> {
  std::vector<EntitySnapshot> snapshots;
  std::vector<EntityHandle> roots;
  m_World.ForEachChunkReadOnly<Ecs::Name, Ecs::Parent, Ecs::ChildList>(
      [this, &snapshots, &roots](std::span<const EntityHandle> handles, std::span<const Ecs::Name> names,
                                 std::span<const Ecs::Parent> parents, std::span<const Ecs::ChildList> links) -> void {
        for (size_t row = 0; row < handles.size(); ++row) {
          snapshots.push_back({.Handle = handles[row],
                               .Parent = parents[row].Id,
                               .Name = names[row].Value,
                               .IsSceneRoot = handles[row] == m_SceneRoot});
          if (parents[row].Id == InvalidEntityHandle && links[row].NextSibling == InvalidEntityHandle) {
            roots.push_back(handles[row]);
          }
        }
      });
  std::ranges::sort(roots, [](EntityHandle left, EntityHandle right) -> bool { return left.Index < right.Index; });
  std::unordered_map<uint64_t, size_t> indices;
  for (size_t i = 0; i < snapshots.size(); ++i) {
    indices.emplace(HandleKey(snapshots[i].Handle), i);
    snapshots[i].HasMeshRenderer = m_World.Has<Ecs::MeshRenderer>(snapshots[i].Handle);
    snapshots[i].HasSpotLight = m_World.Has<Ecs::SpotLight>(snapshots[i].Handle);
    snapshots[i].HasCamera = m_World.Has<Ecs::Camera>(snapshots[i].Handle);
  }

  std::vector<EntitySnapshot> ordered;
  ordered.reserve(snapshots.size());
  std::vector<EntityHandle> pending;
  for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
    pending.push_back(*it);
  }
  while (!pending.empty()) {
    const EntityHandle current = pending.back();
    pending.pop_back();
    const auto found = indices.find(HandleKey(current));
    if (found == indices.end()) {
      continue;
    }
    ordered.push_back(snapshots[found->second]);
    const auto* links = m_World.Get<Ecs::ChildList>(current);
    std::vector<EntityHandle> children;
    for (EntityHandle child = links == nullptr ? InvalidEntityHandle : links->FirstChild;
         child != InvalidEntityHandle;) {
      children.push_back(child);
      const auto* childLinks = m_World.Get<Ecs::ChildList>(child);
      child = childLinks == nullptr ? InvalidEntityHandle : childLinks->NextSibling;
    }
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      pending.push_back(*it);
    }
  }
  return ordered;
}

auto Scene::Update(float deltaTime) -> Status {
  (void)deltaTime;
  (void)sync();
  const Status derived = UpdateDerivedState();
  if (!derived) {
    return derived;
  }
  return m_Scheduler.Execute(m_World, [this] -> Status {
    (void)sync();
    return UpdateDerivedState();
  });
}

} // namespace Engine
