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
/// Owns an ECS world, permanent scene root, hierarchy, command queue, and scheduled systems.
///
/// Mutations requested through Queue methods are applied at sync barriers. This
/// keeps structural changes out of active system iteration.
class ENGINE_API Scene {
public:
  /// Components initialized when a queued entity is created.
  enum class EntityPreset : unsigned char {
    Empty,     ///< Create an entity with only required hierarchy components.
    Mesh,      ///< Create an entity with a mesh renderer.
    SpotLight, ///< Create an entity with a spot light.
    Camera     ///< Create an entity with a camera.
  };
  /// Read-only hierarchy data for editor and inspection clients.
  struct EntitySnapshot {
    EntityHandle Handle;          ///< Entity represented by this snapshot.
    EntityHandle Parent;          ///< Parent entity, or the invalid handle for a root.
    std::string Name;             ///< User-facing entity name.
    bool HasMeshRenderer = false; ///< Whether the entity has a mesh renderer.
    bool HasSpotLight = false;    ///< Whether the entity has a spot light.
    bool HasCamera = false;       ///< Whether the entity has a camera.
    bool IsSceneRoot = false;     ///< Whether this entity is the permanent scene root.
  };

  /// Outcome of one queued structural command.
  struct SyncResult {
    uint64_t Sequence = 0;                     ///< Sequence returned when the command was queued.
    EntityHandle Entity = InvalidEntityHandle; ///< Entity created or modified by the command.
    bool EntityCreated = false;                ///< Whether the command created an entity.
    Status Outcome;                            ///< Command result, including any operation error.
  };
  /// Compatibility name for the hierarchy snapshot record.
  using EcsEntitySnapshot = EntitySnapshot;
  /// Compatibility name for a queued structural command result.
  using StructuralCommandResult = SyncResult;

  /// Creates an empty scene with a permanent, non-deletable root entity.
  Scene();
  Scene(const Scene&) = delete;
  auto operator=(const Scene&) -> Scene& = delete;

  /// Gets the scene-owned archetype world.
  /// @return Read-only world access.
  [[nodiscard]] auto World() const noexcept -> const ArchetypeWorld& { return m_World; }
  /// Gets the scene-owned archetype world through its explicit accessor name.
  /// @return Read-only world access.
  [[nodiscard]] auto GetArchetypeWorld() const noexcept -> const ArchetypeWorld& { return m_World; }
  /// Gets the scheduler for registering scene systems.
  /// @return Mutable scheduler reference.
  [[nodiscard]] auto Scheduler() noexcept -> SystemScheduler& { return m_Scheduler; }
  /// Gets the scheduler through its explicit accessor name.
  /// @return Mutable scheduler reference.
  [[nodiscard]] auto GetSystemScheduler() noexcept -> SystemScheduler& { return m_Scheduler; }
  /// Gets the current scene epoch used to reject stale queued commands.
  /// @return Monotonically increasing scene epoch.
  [[nodiscard]] auto Epoch() const noexcept -> uint64_t { return m_SceneEpoch; }
  /// Gets the current scene epoch through its explicit accessor name.
  /// @return Monotonically increasing scene epoch.
  [[nodiscard]] auto GetSceneEpoch() const noexcept -> uint64_t { return m_SceneEpoch; }
  /// Gets the permanent scene root entity.
  /// @return Handle of the permanent root entity.
  [[nodiscard]] auto SceneRootEntity() const noexcept -> EntityHandle { return m_SceneRoot; }
  /// Checks whether an entity is the permanent scene root.
  /// @param entity Entity handle to check.
  /// @return True when entity is the scene root.
  [[nodiscard]] auto IsSceneRoot(EntityHandle entity) const noexcept -> bool { return entity == m_SceneRoot; }
  /// Gets the configured scale from meters to scene units.
  /// @return Scene units per meter.
  [[nodiscard]] auto UnitsPerMeter() const noexcept -> float { return m_UnitsPerMeter; }
  /// Gets the configured scale through its explicit accessor name.
  /// @return Scene units per meter.
  [[nodiscard]] auto GetUnitsPerMeter() const noexcept -> float { return m_UnitsPerMeter; }
  /// Sets the scale from meters to scene units.
  /// @param unitsPerMeter Positive finite number of scene units per meter.
  void SetUnitsPerMeter(float unitsPerMeter) noexcept { m_UnitsPerMeter = unitsPerMeter; }

  // Queue methods only append owning commands. They never mutate world storage.
  /// Queue entity creation; the returned sequence identifies its sync result.
  /// @param name User-facing entity name.
  /// @param parent Parent entity; the invalid handle attaches the entity beneath the scene root.
  /// @param preset Components to initialize on the new entity.
  /// @return Sequence number for the next synchronization result.
  [[nodiscard]] auto QueueCreate(std::string name, EntityHandle parent = InvalidEntityHandle,
                                 EntityPreset preset = EntityPreset::Empty) -> uint64_t;
  /// Queue entity destruction for the next synchronization barrier; the scene root is protected.
  /// @param entity Entity to destroy.
  /// @return Sequence number for the command result.
  [[nodiscard]] auto QueueDestroy(EntityHandle entity) -> uint64_t;
  /// Queue reparenting; an invalid parent attaches beneath the scene root, and the root cannot move.
  /// @param entity Entity to reparent.
  /// @param parent New parent, or the invalid handle to attach beneath the scene root.
  /// @return Sequence number for the command result.
  [[nodiscard]] auto QueueReparent(EntityHandle entity, EntityHandle parent) -> uint64_t;
  /// Compatibility alias for QueueCreate.
  /// @param name User-facing entity name.
  /// @param parent Parent entity or the invalid handle for a root.
  /// @param preset Components to initialize on the entity.
  /// @return Sequence number for the command result.
  [[nodiscard]] auto QueueCreateEcsEntity(std::string name, EntityHandle parent = InvalidEntityHandle,
                                          EntityPreset preset = EntityPreset::Empty) -> uint64_t {
    return QueueCreate(std::move(name), parent, preset);
  }
  /// Compatibility alias for QueueDestroy.
  /// @param entity Entity to destroy.
  /// @return Sequence number for the command result.
  [[nodiscard]] auto QueueDestroyEcsEntity(EntityHandle entity) -> uint64_t { return QueueDestroy(entity); }
  /// Compatibility alias for QueueReparent.
  /// @param entity Entity to reparent.
  /// @param parent New parent or the invalid handle for a root.
  /// @return Sequence number for the command result.
  [[nodiscard]] auto QueueReparentEcsEntity(EntityHandle entity, EntityHandle parent) -> uint64_t {
    return QueueReparent(entity, parent);
  }

  /// Queues adding a component after validating the component and entity.
  /// @tparam Component Component type to add.
  /// @param entity Entity receiving the component.
  /// @param component Component value to store.
  /// @return Sequence number for the synchronization result.
  template <typename Component>
    requires(std::is_nothrow_move_constructible_v<Component> && std::is_nothrow_destructible_v<Component>)
  [[nodiscard]] auto QueueAdd(EntityHandle entity, Component component) -> uint64_t {
    return QueueCommand(entity, [entity, component = std::move(component)](Scene& scene, EntityHandle&) mutable {
      if constexpr (std::is_same_v<Component, Ecs::SceneRoot> || std::is_same_v<Component, Ecs::Parent> ||
                    std::is_same_v<Component, Ecs::ChildList> || std::is_same_v<Component, Ecs::LocalTransform> ||
                    std::is_same_v<Component, Ecs::WorldTransform> || std::is_same_v<Component, Ecs::LocalVisibility> ||
                    std::is_same_v<Component, Ecs::GlobalVisibility> ||
                    std::is_same_v<Component, Ecs::PersistentIdentity>) {
        (void)scene;
        return Status(std::unexpected(MakeError("Required hierarchy and derived components cannot be added directly")));
      } else {
        if (scene.IsSceneRoot(entity) &&
            (std::is_same_v<Component, Ecs::MeshRenderer> || std::is_same_v<Component, Ecs::SpotLight> ||
             std::is_same_v<Component, Ecs::Camera>)) {
          return Status(
              std::unexpected(MakeError("Renderable and authored components cannot be added to the scene root")));
        }
        const Status valid = scene.ValidateComponent(entity, component);
        if (!valid) {
          return valid;
        }
        return scene.m_World.Add<Component>(entity, std::move(component));
      }
    });
  }
  template <typename Component>
  /// Compatibility alias for QueueAdd.
  /// @tparam Component Component type to add.
  /// @param entity Entity receiving the component.
  /// @param component Component value to store.
  /// @return Sequence number for the synchronization result.
  [[nodiscard]] auto QueueAddEcsComponent(EntityHandle entity, Component component) -> uint64_t {
    return QueueAdd<Component>(entity, std::move(component));
  }

  /// Queues removal of a component from an entity.
  /// @tparam Component Component type to remove.
  /// @param entity Entity from which to remove the component.
  /// @return Sequence number for the synchronization result.
  template <typename Component>
    requires(std::is_nothrow_move_constructible_v<Component> && std::is_nothrow_destructible_v<Component>)
  [[nodiscard]] auto QueueRemove(EntityHandle entity) -> uint64_t {
    return QueueCommand(entity, [entity](Scene& scene, EntityHandle&) {
      if constexpr (std::is_same_v<Component, Ecs::SceneRoot> || std::is_same_v<Component, Ecs::Parent> ||
                    std::is_same_v<Component, Ecs::ChildList> || std::is_same_v<Component, Ecs::LocalTransform> ||
                    std::is_same_v<Component, Ecs::WorldTransform> || std::is_same_v<Component, Ecs::LocalVisibility> ||
                    std::is_same_v<Component, Ecs::GlobalVisibility> ||
                    std::is_same_v<Component, Ecs::PersistentIdentity>) {
        (void)scene;
        return Status(std::unexpected(MakeError("Required scene components cannot be removed")));
      } else {
        if (scene.IsSceneRoot(entity) &&
            (std::is_same_v<Component, Ecs::MeshRenderer> || std::is_same_v<Component, Ecs::SpotLight> ||
             std::is_same_v<Component, Ecs::Camera>)) {
          return Status(std::unexpected(MakeError("The scene root cannot have authored components removed")));
        }
        return scene.m_World.Remove<Component>(entity);
      }
    });
  }
  /// Compatibility alias for QueueRemove.
  /// @tparam Component Component type to remove.
  /// @param entity Entity from which to remove the component.
  /// @return Sequence number for the synchronization result.
  template <typename Component> [[nodiscard]] auto QueueRemoveEcsComponent(EntityHandle entity) -> uint64_t {
    return QueueRemove<Component>(entity);
  }

  /// Queues replacing an existing component after validating the new value.
  /// @tparam Component Component type to replace.
  /// @param entity Entity whose component is replaced.
  /// @param component New component value.
  /// @return Sequence number for the synchronization result.
  template <typename Component>
    requires(std::is_nothrow_move_constructible_v<Component> && std::is_nothrow_destructible_v<Component> &&
             std::is_nothrow_move_assignable_v<Component>)
  [[nodiscard]] auto QueueSet(EntityHandle entity, Component component) -> uint64_t {
    return QueueCommand(entity, [entity, component = std::move(component)](Scene& scene, EntityHandle&) mutable {
      if constexpr (std::is_same_v<Component, Ecs::SceneRoot> || std::is_same_v<Component, Ecs::Parent> ||
                    std::is_same_v<Component, Ecs::ChildList> || std::is_same_v<Component, Ecs::WorldTransform> ||
                    std::is_same_v<Component, Ecs::GlobalVisibility>) {
        (void)scene;
        return Status(std::unexpected(MakeError("Hierarchy links and derived state are system-owned")));
      } else {
        if (scene.IsSceneRoot(entity) &&
            (std::is_same_v<Component, Ecs::MeshRenderer> || std::is_same_v<Component, Ecs::SpotLight> ||
             std::is_same_v<Component, Ecs::Camera>)) {
          return Status(
              std::unexpected(MakeError("Renderable and authored components cannot be assigned to the scene root")));
        }
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
  /// Compatibility alias for QueueSet.
  /// @tparam Component Component type to replace.
  /// @param entity Entity whose component is replaced.
  /// @param component New component value.
  /// @return Sequence number for the synchronization result.
  [[nodiscard]] auto QueueSetEcsComponent(EntityHandle entity, Component component) -> uint64_t {
    return QueueSet<Component>(entity, std::move(component));
  }

  /// Replaces a component immediately outside system execution.
  /// @tparam Component Component type to replace.
  /// @param entity Entity whose component is replaced.
  /// @param component New component value.
  /// @return Success, or an error when the edit is disallowed or the component is invalid.
  template <typename Component>
    requires(std::is_nothrow_move_constructible_v<Component> && std::is_nothrow_move_assignable_v<Component>)
  [[nodiscard]] auto SetInPlace(EntityHandle entity, Component component) -> Status {
    if (m_World.IsSystemPhase()) {
      return std::unexpected(MakeError("In-place component edits are not allowed during system execution"));
    }
    if constexpr (std::is_same_v<Component, Ecs::SceneRoot> || std::is_same_v<Component, Ecs::Parent> ||
                  std::is_same_v<Component, Ecs::ChildList> || std::is_same_v<Component, Ecs::WorldTransform> ||
                  std::is_same_v<Component, Ecs::GlobalVisibility>) {
      return std::unexpected(MakeError("Scene identity, hierarchy links, and derived state are system-owned"));
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
  /// @return Results for commands applied during this call.
  [[nodiscard]] auto sync() -> std::vector<SyncResult>;
  /// Compatibility alias for sync.
  /// @return Results for commands applied during this call.
  [[nodiscard]] auto DrainStructuralCommands() -> std::vector<SyncResult> { return sync(); }
  /// Builds a read-only hierarchy snapshot, with the permanent scene root first.
  /// @return Snapshot records ordered by hierarchy traversal.
  [[nodiscard]] auto HierarchySnapshot() const -> std::vector<EntitySnapshot>;
  /// Compatibility alias for HierarchySnapshot.
  /// @return Snapshot records ordered by hierarchy traversal.
  [[nodiscard]] auto GetEcsHierarchySnapshot() const -> std::vector<EntitySnapshot> { return HierarchySnapshot(); }
  /// Gets the number of live entities, including the permanent scene root.
  /// @return Current entity count.
  [[nodiscard]] auto EntityCount() const noexcept -> size_t { return m_World.EntityCount(); }

  /// Apply queued work and run registered systems for one frame update.
  /// Structural writes are synchronized between system phases.
  /// @param deltaTime Elapsed frame time in seconds.
  /// @return Success, or the first error reported by synchronization or a system.
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
    if (IsSceneRoot(entity) && (std::is_same_v<Component, Ecs::MeshRenderer> ||
                                std::is_same_v<Component, Ecs::SpotLight> || std::is_same_v<Component, Ecs::Camera>)) {
      return std::unexpected(MakeError("Renderable and authored components are not valid on the scene root"));
    }
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
  EntityHandle m_SceneRoot = InvalidEntityHandle;
};
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

} // namespace Engine
