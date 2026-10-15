#pragma once

#include "runtime/include/CoreExport.h"
#include "scene/include/ArchetypeWorld.h"
#include "scene/include/SceneIds.h"
#include "scene/include/SystemScheduler.h"
#include "scene/include/components/EcsComponents.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine {

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251)
#  pragma warning(disable : 4324)
#endif
/// Owns an ECS world and its hierarchy, command queue, and scheduled systems.
///
/// Mutations requested through Queue methods are applied at sync barriers. This
/// keeps structural changes out of active system iteration.
class ENGINE_API Scene {
public:
  /// Components initialized when a queued entity is created.
  enum class EntityPreset : unsigned char { Empty, Mesh, SpotLight, Camera };
  /// Read-only hierarchy data for editor and inspection clients.
  struct EntitySnapshot {
    EntityHandle Handle;
    EntityHandle Parent;
    std::string Name;
    bool HasMeshRenderer = false;
    bool HasSpotLight = false;
    bool HasCamera = false;
  };

  /// Outcome of one queued structural command.
  struct SyncResult {
    uint64_t Sequence = 0;
    EntityHandle Entity = InvalidEntityHandle;
    bool EntityCreated = false;
    Status Outcome;
  };
  using EcsEntitySnapshot = EntitySnapshot;
  using StructuralCommandResult = SyncResult;

  Scene() = default;
  Scene(const Scene&) = delete;
  auto operator=(const Scene&) -> Scene& = delete;

  [[nodiscard]] auto World() const noexcept -> const ArchetypeWorld& { return m_World; }
  [[nodiscard]] auto GetArchetypeWorld() const noexcept -> const ArchetypeWorld& { return m_World; }
  [[nodiscard]] auto Scheduler() noexcept -> SystemScheduler& { return m_Scheduler; }
  [[nodiscard]] auto GetSystemScheduler() noexcept -> SystemScheduler& { return m_Scheduler; }
  [[nodiscard]] auto Epoch() const noexcept -> uint64_t { return m_SceneEpoch; }
  [[nodiscard]] auto GetSceneEpoch() const noexcept -> uint64_t { return m_SceneEpoch; }
  [[nodiscard]] auto UnitsPerMeter() const noexcept -> float { return m_UnitsPerMeter; }
  [[nodiscard]] auto GetUnitsPerMeter() const noexcept -> float { return m_UnitsPerMeter; }
  void SetUnitsPerMeter(float unitsPerMeter) noexcept { m_UnitsPerMeter = unitsPerMeter; }

  // Queue methods only append owning commands. They never mutate world storage.
  /// Queue entity creation; the returned sequence identifies its sync result.
  [[nodiscard]] auto QueueCreate(std::string name, EntityHandle parent = InvalidEntityHandle,
                                 EntityPreset preset = EntityPreset::Empty) -> uint64_t;
  /// Queue entity destruction for the next synchronization barrier.
  [[nodiscard]] auto QueueDestroy(EntityHandle entity) -> uint64_t;
  /// Queue reparenting; invalid hierarchy changes are reported by sync().
  [[nodiscard]] auto QueueReparent(EntityHandle entity, EntityHandle parent) -> uint64_t;
  [[nodiscard]] auto QueueCreateEcsEntity(std::string name, EntityHandle parent = InvalidEntityHandle,
                                          EntityPreset preset = EntityPreset::Empty) -> uint64_t {
    return QueueCreate(std::move(name), parent, preset);
  }
  [[nodiscard]] auto QueueDestroyEcsEntity(EntityHandle entity) -> uint64_t { return QueueDestroy(entity); }
  [[nodiscard]] auto QueueReparentEcsEntity(EntityHandle entity, EntityHandle parent) -> uint64_t {
    return QueueReparent(entity, parent);
  }

  template <typename Component>
    requires(std::is_nothrow_move_constructible_v<Component> && std::is_nothrow_destructible_v<Component>)
  [[nodiscard]] auto QueueAdd(EntityHandle entity, Component component) -> uint64_t {
    return QueueCommand(entity, [entity, component = std::move(component)](Scene& scene, EntityHandle&) mutable {
      if constexpr (std::is_same_v<Component, Ecs::Parent> || std::is_same_v<Component, Ecs::ChildList> ||
                    std::is_same_v<Component, Ecs::LocalTransform> || std::is_same_v<Component, Ecs::WorldTransform> ||
                    std::is_same_v<Component, Ecs::LocalVisibility> ||
                    std::is_same_v<Component, Ecs::GlobalVisibility> ||
                    std::is_same_v<Component, Ecs::PersistentIdentity>) {
        (void)scene;
        return Status(std::unexpected(MakeError("Required hierarchy and derived components cannot be added directly")));
      } else {
        const Status valid = scene.ValidateComponent(entity, component);
        if (!valid) {
          return valid;
        }
        return scene.m_World.Add<Component>(entity, std::move(component));
      }
    });
  }
  template <typename Component>
  [[nodiscard]] auto QueueAddEcsComponent(EntityHandle entity, Component component) -> uint64_t {
    return QueueAdd<Component>(entity, std::move(component));
  }

  template <typename Component>
    requires(std::is_nothrow_move_constructible_v<Component> && std::is_nothrow_destructible_v<Component>)
  [[nodiscard]] auto QueueRemove(EntityHandle entity) -> uint64_t {
    return QueueCommand(entity, [entity](Scene& scene, EntityHandle&) {
      if constexpr (std::is_same_v<Component, Ecs::Parent> || std::is_same_v<Component, Ecs::ChildList> ||
                    std::is_same_v<Component, Ecs::LocalTransform> || std::is_same_v<Component, Ecs::WorldTransform> ||
                    std::is_same_v<Component, Ecs::LocalVisibility> ||
                    std::is_same_v<Component, Ecs::GlobalVisibility> ||
                    std::is_same_v<Component, Ecs::PersistentIdentity>) {
        (void)scene;
        return Status(std::unexpected(MakeError("Required scene components cannot be removed")));
      } else {
        return scene.m_World.Remove<Component>(entity);
      }
    });
  }
  template <typename Component> [[nodiscard]] auto QueueRemoveEcsComponent(EntityHandle entity) -> uint64_t {
    return QueueRemove<Component>(entity);
  }

  template <typename Component>
    requires(std::is_nothrow_move_constructible_v<Component> && std::is_nothrow_destructible_v<Component> &&
             std::is_nothrow_move_assignable_v<Component>)
  [[nodiscard]] auto QueueSet(EntityHandle entity, Component component) -> uint64_t {
    return QueueCommand(entity, [entity, component = std::move(component)](Scene& scene, EntityHandle&) mutable {
      if constexpr (std::is_same_v<Component, Ecs::Parent> || std::is_same_v<Component, Ecs::ChildList> ||
                    std::is_same_v<Component, Ecs::WorldTransform> ||
                    std::is_same_v<Component, Ecs::GlobalVisibility>) {
        (void)scene;
        return Status(std::unexpected(MakeError("Hierarchy links and derived state are system-owned")));
      } else {
        if constexpr (std::is_same_v<Component, Ecs::LocalTransform>) {
          component.IsDirty = true;
        }
        const Status valid = scene.ValidateComponent(entity, component);
        if (!valid) {
          return valid;
        }
        return scene.m_World.Set<Component>(entity, std::move(component));
      }
    });
  }
  template <typename Component>
  [[nodiscard]] auto QueueSetEcsComponent(EntityHandle entity, Component component) -> uint64_t {
    return QueueSet<Component>(entity, std::move(component));
  }

  template <typename Component>
    requires(std::is_nothrow_move_constructible_v<Component> && std::is_nothrow_move_assignable_v<Component>)
  [[nodiscard]] auto SetInPlace(EntityHandle entity, Component component) -> Status {
    if (m_World.IsSystemPhase()) {
      return std::unexpected(MakeError("In-place component edits are not allowed during system execution"));
    }
    if constexpr (std::is_same_v<Component, Ecs::Parent> || std::is_same_v<Component, Ecs::ChildList> ||
                  std::is_same_v<Component, Ecs::WorldTransform> || std::is_same_v<Component, Ecs::GlobalVisibility>) {
      return std::unexpected(MakeError("Hierarchy links and derived state are system-owned"));
    } else {
      if constexpr (std::is_same_v<Component, Ecs::LocalTransform>) {
        component.IsDirty = true;
      }
      const Status valid = ValidateComponent(entity, component);
      if (!valid) {
        return valid;
      }
      return m_World.Set<Component>(entity, std::move(component));
    }
  }

  /// Apply queued structural changes at an exclusive barrier and return outcomes.
  /// Commands queued during processing are applied in a later wave in the same call.
  [[nodiscard]] auto sync() -> std::vector<SyncResult>;
  [[nodiscard]] auto DrainStructuralCommands() -> std::vector<SyncResult> { return sync(); }
  [[nodiscard]] auto HierarchySnapshot() const -> std::vector<EntitySnapshot>;
  [[nodiscard]] auto GetEcsHierarchySnapshot() const -> std::vector<EntitySnapshot> { return HierarchySnapshot(); }
  [[nodiscard]] auto EntityCount() const noexcept -> size_t { return m_World.EntityCount(); }

  /// Apply queued work and run registered systems for one frame update.
  /// Structural writes are synchronized between system phases.
  [[nodiscard]] auto Update(float deltaTime) -> Status;

private:
  using Command = std::function<Status(Scene&, EntityHandle&)>;
  struct QueuedCommand {
    uint64_t Sequence = 0;
    uint64_t SceneEpoch = 0;
    EntityHandle Entity = InvalidEntityHandle;
    Command Apply;
  };

  [[nodiscard]] auto QueueCommand(EntityHandle entity, Command command) -> uint64_t;
  [[nodiscard]] auto CreateImmediate(const std::string& name, EntityHandle parent) -> Result<EntityHandle>;
  [[nodiscard]] auto DestroyImmediate(EntityHandle entity) -> Status;
  [[nodiscard]] auto ReparentImmediate(EntityHandle entity, EntityHandle parent) -> Status;
  [[nodiscard]] auto AttachChild(EntityHandle parent, EntityHandle child) -> Status;
  [[nodiscard]] auto Detach(EntityHandle entity) -> Status;
  [[nodiscard]] auto RebuildHierarchyIndex() -> Status;
  [[nodiscard]] auto UpdateDerivedState() -> Status;

  struct HierarchyWorkItem {
    EntityHandle Entity = InvalidEntityHandle;
    uint32_t Depth = 0;
    uint32_t ParentWorkIndex = UINT32_MAX;
  };

  template <typename Component>
  [[nodiscard]] auto ValidateComponent(EntityHandle entity, const Component& component) const -> Status {
    if constexpr (requires { Ecs::Validate(component); }) {
      const Status valid = Ecs::Validate(component);
      if (!valid) {
        return valid;
      }
    }
    (void)entity;
    return {};
  }

  ArchetypeWorld m_World;
  SystemScheduler m_Scheduler;
  mutable std::mutex m_CommandMutex;
  std::vector<QueuedCommand> m_Commands;
  std::vector<HierarchyWorkItem> m_HierarchyOrder;
  std::vector<uint32_t> m_LevelOrder;
  std::vector<size_t> m_LevelOffsets;
  std::vector<uint8_t> m_RecomputedThisUpdate;
  uint64_t m_NextSequence = 1;
  uint64_t m_SceneEpoch = 1;
  uint64_t m_TopologyRevision = 1;
  uint64_t m_CachedTopologyRevision = 0;
  bool m_Syncing = false;
  float m_UnitsPerMeter = 1.0F;
};
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

} // namespace Engine
