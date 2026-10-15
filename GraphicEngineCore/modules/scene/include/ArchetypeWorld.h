#pragma once

#include "runtime/include/Result.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine {

class SystemScheduler;

/// Generation-checked handle identifying an entity in one scene world.
struct EntityHandle {
  uint32_t Index = UINT32_MAX; ///< Slot index in the owning scene world.
  uint32_t Generation = 0; ///< Generation used to reject stale handles.

  /// Compares both the slot index and generation.
  /// @param other Handle to compare with.
  /// @return True when both handles identify the same generation of one slot.
  auto operator==(const EntityHandle& other) const noexcept -> bool = default;
};

inline constexpr EntityHandle InvalidEntityHandle{};

/// Scene-local archetype storage for entities and their components.
///
/// Access is confined to the owning scene thread. Stored component types must
/// support non-throwing move construction and destruction so migration can
/// prepare a destination before changing its source.
class ArchetypeWorld {
private:
  using ComponentTypeId = uint32_t;

  template <typename Component>
  static constexpr bool IsStoredComponent = std::is_nothrow_move_constructible_v<Component> &&
                                            std::is_nothrow_destructible_v<Component> && !std::is_array_v<Component>;

public:
  static constexpr size_t ChunkCapacity = 256; ///< Maximum entities stored in one archetype chunk.

  ArchetypeWorld() = default;
  ArchetypeWorld(const ArchetypeWorld&) = delete;
  auto operator=(const ArchetypeWorld&) -> ArchetypeWorld& = delete;
  ArchetypeWorld(ArchetypeWorld&&) = delete;
  auto operator=(ArchetypeWorld&&) -> ArchetypeWorld& = delete;

  /// Creates an entity with one value for each component type.
  /// @param components Initial component values; each type must be unique and nothrow-movable.
  /// @return The new handle, or an error when creation is rejected or allocation fails.
  template <typename... Components>
    requires(sizeof...(Components) > 0 && (IsStoredComponent<Components> && ...))
  auto Create(Components... components) -> Result<EntityHandle> {
    if (m_SystemPhase) {
      return std::unexpected(MakeError("Structural changes during system execution must be queued for sync"));
    }
    if (m_QueryDepth != 0) {
      return std::unexpected(MakeError("Structural changes are not allowed during an archetype query"));
    }
    try {
      const std::array ids{GetOrRegister<Components>()...};
      const auto signature = CanonicalSignature(ids);
      Archetype& archetype = GetOrCreateArchetype(signature);
      Chunk& chunk = GetWritableChunk(archetype);
      const EntityHandle entity = AcquireEntity();
      std::tuple<Components...> values(std::move(components)...);
      AppendValues(chunk, archetype, values, std::index_sequence_for<Components...>{});
      chunk.Entities.push_back(entity);
      SetLocation(entity, archetype.Id, archetype.Chunks.size() - 1, chunk.Entities.size() - 1);
      return entity;
    } catch (const std::exception& error) {
      return std::unexpected(MakeError(error.what()));
    } catch (...) {
      return std::unexpected(MakeError("Failed to create entity in archetype world"));
    }
  }

  /// Checks that a handle refers to a live entity in this world.
  /// @param entity Handle to validate.
  /// @return True when the slot is alive and its generation matches.
  auto IsAlive(EntityHandle entity) const noexcept -> bool {
    return entity.Index < m_Entities.size() && m_Entities[entity.Index].Alive &&
           m_Entities[entity.Index].Generation == entity.Generation;
  }

  /// Gets the number of live entities.
  /// @return Current live entity count.
  auto EntityCount() const noexcept -> size_t { return m_AliveCount; }
  /// Checks whether systems are currently executing.
  /// @return True while structural changes must be queued for synchronization.
  auto IsSystemPhase() const noexcept -> bool { return m_SystemPhase; }

  /// Checks whether a live entity has a component of the requested type.
  /// @param entity Entity to inspect.
  /// @return True when the entity is live and has the component.
  template <typename Component>
    requires IsStoredComponent<Component>
  auto Has(EntityHandle entity) const noexcept -> bool {
    if (!IsAlive(entity)) {
      return false;
    }
    const auto type = FindType(std::type_index(typeid(Component)));
    return type.has_value() && Contains(m_Archetypes[m_Entities[entity.Index].Position.Archetype].Signature, *type);
  }

  /// Gets a mutable component pointer, valid only until the next structural change.
  /// @param entity Entity containing the component.
  /// @return Component address, or null for a stale entity or missing component.
  template <typename Component>
    requires IsStoredComponent<Component>
  auto Get(EntityHandle entity) noexcept -> Component* {
    return const_cast<Component*>(std::as_const(*this).Get<Component>(entity));
  }

  /// Gets a read-only component pointer, valid only until the next structural change.
  /// @param entity Entity containing the component.
  /// @return Component address, or null for a stale entity or missing component.
  template <typename Component>
    requires IsStoredComponent<Component>
  auto Get(EntityHandle entity) const noexcept -> const Component* {
    if (!IsAlive(entity)) {
      return nullptr;
    }
    const auto type = FindType(std::type_index(typeid(Component)));
    if (!type) {
      return nullptr;
    }
    const Location& location = m_Entities[entity.Index].Position;
    const Archetype& archetype = m_Archetypes[location.Archetype];
    if (!Contains(archetype.Signature, *type)) {
      return nullptr;
    }
    const Chunk& chunk = *archetype.Chunks[location.Chunk];
    return static_cast<const TypedColumn<Component>&>(*chunk.Columns[ColumnIndex(archetype, *type)]).Values.data() +
           location.Row;
  }

  /// Replaces an entity's existing component value.
  /// @param entity Entity whose component is replaced.
  /// @param component New component value.
  /// @return Success, or an error when the entity is stale, lacks the component, or is being queried.
  template <typename Component>
    requires(IsStoredComponent<Component> && std::is_nothrow_move_assignable_v<Component>)
  auto Set(EntityHandle entity, Component component) -> Status {
    if (m_QueryDepth != 0) {
      return std::unexpected(MakeError("Component edits are not allowed during an archetype query"));
    }
    Component* destination = Get<Component>(entity);
    if (destination == nullptr) {
      return std::unexpected(MakeError("Cannot edit a missing component or stale entity handle"));
    }
    *destination = std::move(component);
    return {};
  }

  /// Adds a component by migrating the entity to its new archetype.
  /// @param entity Entity receiving the component.
  /// @param component New component value.
  /// @return Success, or an error when the operation is disallowed or allocation fails.
  template <typename Component>
    requires IsStoredComponent<Component>
  auto Add(EntityHandle entity, Component component) -> Status {
    if (m_SystemPhase) {
      return std::unexpected(MakeError("Structural changes during system execution must be queued for sync"));
    }
    if (m_QueryDepth != 0) {
      return std::unexpected(MakeError("Structural changes are not allowed during an archetype query"));
    }
    if (!IsAlive(entity)) {
      return std::unexpected(MakeError("Cannot add a component to a stale entity handle"));
    }
    if (Has<Component>(entity)) {
      return std::unexpected(MakeError("Entity already has this component"));
    }
    try {
      const ComponentTypeId addedType = GetOrRegister<Component>();
      const Location oldLocation = m_Entities[entity.Index].Position;
      auto signature = m_Archetypes[oldLocation.Archetype].Signature;
      signature.insert(std::lower_bound(signature.begin(), signature.end(), addedType), addedType);
      Archetype& destinationArchetype = GetOrCreateArchetype(signature);
      Chunk& destinationChunk = GetWritableChunk(destinationArchetype);
      Archetype& sourceArchetype = m_Archetypes[oldLocation.Archetype];
      Chunk& sourceChunk = *sourceArchetype.Chunks[oldLocation.Chunk];
      const size_t destinationRow = destinationChunk.Entities.size();

      // The new value is constructed before retained source values are moved.
      auto& addedColumn =
          static_cast<TypedColumn<Component>&>(*destinationChunk.Columns[ColumnIndex(destinationArchetype, addedType)]);
      addedColumn.Values.emplace_back(std::move(component));
      for (size_t column = 0; column < signature.size(); ++column) {
        const ComponentTypeId type = signature[column];
        if (type != addedType) {
          destinationChunk.Columns[column]->AppendMoveFrom(*sourceChunk.Columns[ColumnIndex(sourceArchetype, type)],
                                                           oldLocation.Row);
        }
      }
      destinationChunk.Entities.push_back(entity);
      SetLocation(entity, destinationArchetype.Id, destinationArchetype.Chunks.size() - 1, destinationRow);
      RemoveRow(sourceArchetype, oldLocation.Chunk, oldLocation.Row, entity);
      return {};
    } catch (const std::exception& error) {
      return std::unexpected(MakeError(error.what()));
    } catch (...) {
      return std::unexpected(MakeError("Failed to add component to entity"));
    }
  }

  /// Removes a component by migrating the entity to its new archetype.
  /// @param entity Entity from which to remove the component.
  /// @return Success, or an error when the operation is disallowed or the component is absent.
  template <typename Component>
    requires IsStoredComponent<Component>
  auto Remove(EntityHandle entity) -> Status {
    if (m_SystemPhase) {
      return std::unexpected(MakeError("Structural changes during system execution must be queued for sync"));
    }
    if (m_QueryDepth != 0) {
      return std::unexpected(MakeError("Structural changes are not allowed during an archetype query"));
    }
    if (!IsAlive(entity)) {
      return std::unexpected(MakeError("Cannot remove a component from a stale entity handle"));
    }
    try {
      const auto type = FindType(std::type_index(typeid(Component)));
      if (!type || !Has<Component>(entity)) {
        return std::unexpected(MakeError("Entity does not have this component"));
      }
      const Location oldLocation = m_Entities[entity.Index].Position;
      auto signature = m_Archetypes[oldLocation.Archetype].Signature;
      signature.erase(std::lower_bound(signature.begin(), signature.end(), *type));
      Archetype& destinationArchetype = GetOrCreateArchetype(signature);
      Chunk& destinationChunk = GetWritableChunk(destinationArchetype);
      Archetype& sourceArchetype = m_Archetypes[oldLocation.Archetype];
      Chunk& sourceChunk = *sourceArchetype.Chunks[oldLocation.Chunk];
      const size_t destinationRow = destinationChunk.Entities.size();
      for (size_t column = 0; column < signature.size(); ++column) {
        const ComponentTypeId retainedType = signature[column];
        destinationChunk.Columns[column]->AppendMoveFrom(
            *sourceChunk.Columns[ColumnIndex(sourceArchetype, retainedType)], oldLocation.Row);
      }
      destinationChunk.Entities.push_back(entity);
      SetLocation(entity, destinationArchetype.Id, destinationArchetype.Chunks.size() - 1, destinationRow);
      RemoveRow(sourceArchetype, oldLocation.Chunk, oldLocation.Row, entity);
      return {};
    } catch (const std::exception& error) {
      return std::unexpected(MakeError(error.what()));
    } catch (...) {
      return std::unexpected(MakeError("Failed to remove component from entity"));
    }
  }

  /// Destroys an entity and invalidates its current handle generation.
  /// @param entity Entity to destroy.
  /// @return Success, or an error when destruction is disallowed or the handle is stale.
  auto Destroy(EntityHandle entity) -> Status {
    if (m_SystemPhase) {
      return std::unexpected(MakeError("Structural changes during system execution must be queued for sync"));
    }
    if (m_QueryDepth != 0) {
      return std::unexpected(MakeError("Structural changes are not allowed during an archetype query"));
    }
    if (!IsAlive(entity)) {
      return std::unexpected(MakeError("Cannot destroy a stale entity handle"));
    }
    try {
      m_FreeEntities.reserve(m_FreeEntities.size() + 1);
      const Location location = m_Entities[entity.Index].Position;
      Archetype& archetype = m_Archetypes[location.Archetype];
      RemoveRow(archetype, location.Chunk, location.Row, entity);
      EntityRecord& record = m_Entities[entity.Index];
      record.Alive = false;
      ++record.Generation;
      if (record.Generation == 0) {
        ++record.Generation;
      }
      m_FreeEntities.push_back(entity.Index);
      --m_AliveCount;
      return {};
    } catch (const std::exception& error) {
      return std::unexpected(MakeError(error.what()));
    } catch (...) {
      return std::unexpected(MakeError("Failed to destroy entity"));
    }
  }

  /// Invokes a callback for each non-empty chunk matching the component set.
  /// @param function Callback receiving entity handles and mutable component spans.
  template <typename... Components, typename Function>
    requires(sizeof...(Components) > 0 && (IsStoredComponent<Components> && ...) &&
             std::invocable<Function, std::span<const EntityHandle>, std::span<Components>...>)
  void ForEachChunk(Function&& function) {
    QueryGuard guard(*this);
    const auto queryTypes = ResolveTypes<Components...>();
    if (!queryTypes) {
      return;
    }
    for (Archetype& archetype : m_Archetypes) {
      if (!Includes(archetype.Signature, *queryTypes)) {
        continue;
      }
      const auto columnIndices = ResolveColumns<Components...>(archetype);
      for (auto& chunk : archetype.Chunks) {
        if (chunk->Entities.empty()) {
          continue;
        }
        InvokeChunk<Components...>(std::forward<Function>(function), *chunk, columnIndices,
                                   std::index_sequence_for<Components...>{});
      }
    }
  }

  /// Invokes a callback for each non-empty chunk using read-only component spans.
  /// @param function Callback receiving entity handles and const component spans.
  template <typename... Components, typename Function>
    requires(sizeof...(Components) > 0 && (IsStoredComponent<Components> && ...) &&
             std::invocable<Function, std::span<const EntityHandle>, std::span<const Components>...>)
  void ForEachChunkReadOnly(Function&& function) const {
    QueryGuard guard(*this);
    const auto queryTypes = ResolveTypes<Components...>();
    if (!queryTypes) {
      return;
    }
    for (const Archetype& archetype : m_Archetypes) {
      if (!Includes(archetype.Signature, *queryTypes)) {
        continue;
      }
      const auto columnIndices = ResolveColumns<Components...>(archetype);
      for (const auto& chunk : archetype.Chunks) {
        if (chunk->Entities.empty()) {
          continue;
        }
        InvokeReadOnlyChunk<Components...>(std::forward<Function>(function), *chunk, columnIndices,
                                           std::index_sequence_for<Components...>{});
      }
    }
  }

private:
  friend class SystemScheduler;

  void BeginSystemPhase() noexcept { m_SystemPhase = true; }
  void EndSystemPhase() noexcept { m_SystemPhase = false; }

  struct Location {
    size_t Archetype = 0;
    size_t Chunk = 0;
    size_t Row = 0;
  };
  struct EntityRecord {
    uint32_t Generation = 1;
    bool Alive = false;
    Location Position{};
  };
  struct ColumnBase {
    virtual ~ColumnBase() = default;
    virtual void Reserve(size_t capacity) = 0;
    virtual void AppendMoveFrom(ColumnBase& source, size_t row) noexcept = 0;
    virtual void RemoveSwap(size_t row) noexcept = 0;
  };
  template <typename Component> struct TypedColumn final : ColumnBase {
    std::vector<Component> Values;
    void Reserve(size_t capacity) override { Values.reserve(capacity); }
    void AppendMoveFrom(ColumnBase& source, size_t row) noexcept override {
      auto& typedSource = static_cast<TypedColumn&>(source);
      Values.emplace_back(std::move(typedSource.Values[row]));
    }
    void RemoveSwap(size_t row) noexcept override {
      const size_t last = Values.size() - 1;
      if (row != last) {
        std::destroy_at(Values.data() + row);
        std::construct_at(Values.data() + row, std::move(Values[last]));
      }
      Values.pop_back();
    }
  };
  struct ComponentDescriptor {
    std::unique_ptr<ColumnBase> (*CreateColumn)() = nullptr;
  };
  struct Chunk {
    std::vector<EntityHandle> Entities;
    std::vector<std::unique_ptr<ColumnBase>> Columns;
  };
  struct Archetype {
    size_t Id = 0;
    std::vector<ComponentTypeId> Signature;
    std::vector<std::unique_ptr<Chunk>> Chunks;
  };
  class QueryGuard {
  public:
    explicit QueryGuard(const ArchetypeWorld& world) : m_World(world) { ++m_World.m_QueryDepth; }
    ~QueryGuard() { --m_World.m_QueryDepth; }

  private:
    const ArchetypeWorld& m_World;
  };

  template <typename Component> auto GetOrRegister() -> ComponentTypeId {
    const std::type_index key(typeid(Component));
    if (auto it = m_TypeIds.find(key); it != m_TypeIds.end()) {
      return it->second;
    }
    const auto id = static_cast<ComponentTypeId>(m_Descriptors.size());
    m_Descriptors.push_back(
        {[]() -> std::unique_ptr<ColumnBase> { return std::make_unique<TypedColumn<Component>>(); }});
    m_TypeIds.emplace(key, id);
    return id;
  }

  auto FindType(std::type_index key) const noexcept -> std::optional<ComponentTypeId> {
    const auto it = m_TypeIds.find(key);
    return it == m_TypeIds.end() ? std::nullopt : std::optional<ComponentTypeId>(it->second);
  }

  static auto CanonicalSignature(std::span<const ComponentTypeId> types) -> std::vector<ComponentTypeId> {
    std::vector<ComponentTypeId> signature(types.begin(), types.end());
    std::sort(signature.begin(), signature.end());
    signature.erase(std::unique(signature.begin(), signature.end()), signature.end());
    if (signature.size() != types.size()) {
      throw std::invalid_argument("An entity cannot contain a component type more than once");
    }
    return signature;
  }

  static auto Contains(const std::vector<ComponentTypeId>& signature, ComponentTypeId type) noexcept -> bool {
    return std::binary_search(signature.begin(), signature.end(), type);
  }
  static auto Includes(const std::vector<ComponentTypeId>& signature,
                       const std::vector<ComponentTypeId>& query) noexcept -> bool {
    return std::includes(signature.begin(), signature.end(), query.begin(), query.end());
  }
  static auto ColumnIndex(const Archetype& archetype, ComponentTypeId type) noexcept -> size_t {
    return static_cast<size_t>(std::lower_bound(archetype.Signature.begin(), archetype.Signature.end(), type) -
                               archetype.Signature.begin());
  }

  auto GetOrCreateArchetype(const std::vector<ComponentTypeId>& signature) -> Archetype& {
    for (Archetype& archetype : m_Archetypes) {
      if (archetype.Signature == signature) {
        return archetype;
      }
    }
    Archetype archetype;
    archetype.Id = m_Archetypes.size();
    archetype.Signature = signature;
    m_Archetypes.push_back(std::move(archetype));
    return m_Archetypes.back();
  }

  auto GetWritableChunk(Archetype& archetype) -> Chunk& {
    if (!archetype.Chunks.empty() && archetype.Chunks.back()->Entities.size() < ChunkCapacity) {
      return *archetype.Chunks.back();
    }
    auto chunk = std::make_unique<Chunk>();
    chunk->Entities.reserve(ChunkCapacity);
    chunk->Columns.reserve(archetype.Signature.size());
    for (ComponentTypeId type : archetype.Signature) {
      auto column = m_Descriptors[type].CreateColumn();
      column->Reserve(ChunkCapacity);
      chunk->Columns.push_back(std::move(column));
    }
    archetype.Chunks.push_back(std::move(chunk));
    return *archetype.Chunks.back();
  }

  template <typename Component, typename Value>
  static void AppendValue(Chunk& chunk, const Archetype& archetype, ComponentTypeId type, Value&& value) noexcept {
    const size_t columnIndex = ColumnIndex(archetype, type);
    auto& column = static_cast<TypedColumn<Component>&>(*chunk.Columns[columnIndex]);
    column.Values.emplace_back(std::move(value));
  }

  template <typename Tuple, size_t... Indices>
  void AppendValues(Chunk& chunk, const Archetype& archetype, Tuple& values, std::index_sequence<Indices...>) noexcept {
    (AppendValue<std::tuple_element_t<Indices, Tuple>>(
         chunk, archetype, GetOrRegister<std::tuple_element_t<Indices, Tuple>>(), std::get<Indices>(values)),
     ...);
  }

  auto AcquireEntity() -> EntityHandle {
    if (!m_FreeEntities.empty()) {
      const uint32_t index = m_FreeEntities.back();
      m_FreeEntities.pop_back();
      m_Entities[index].Alive = true;
      ++m_AliveCount;
      return {index, m_Entities[index].Generation};
    }
    const uint32_t index = static_cast<uint32_t>(m_Entities.size());
    m_Entities.push_back({});
    m_Entities.back().Alive = true;
    ++m_AliveCount;
    return {index, m_Entities.back().Generation};
  }
  void SetLocation(EntityHandle entity, size_t archetype, size_t chunk, size_t row) noexcept {
    m_Entities[entity.Index].Position = {archetype, chunk, row};
  }

  void RemoveRow(Archetype& archetype, size_t chunkIndex, size_t row, EntityHandle removed) noexcept {
    Chunk& chunk = *archetype.Chunks[chunkIndex];
    const size_t last = chunk.Entities.size() - 1;
    const EntityHandle moved = chunk.Entities[last];
    for (auto& column : chunk.Columns) {
      column->RemoveSwap(row);
    }
    if (row != last) {
      chunk.Entities[row] = moved;
      if (moved != removed) {
        SetLocation(moved, archetype.Id, chunkIndex, row);
      }
    }
    chunk.Entities.pop_back();
  }

  template <typename... Components> auto ResolveTypes() const -> std::optional<std::vector<ComponentTypeId>> {
    std::vector<ComponentTypeId> types;
    types.reserve(sizeof...(Components));
    bool found = true;
    (
        [&] {
          const auto type = FindType(std::type_index(typeid(Components)));
          if (type) {
            types.push_back(*type);
          } else {
            found = false;
          }
        }(),
        ...);
    if (!found) {
      return std::nullopt;
    }
    std::sort(types.begin(), types.end());
    return types;
  }

  template <typename... Components> auto ResolveColumns(const Archetype& archetype) const -> std::vector<size_t> {
    std::vector<size_t> columns;
    columns.reserve(sizeof...(Components));
    (columns.push_back(ColumnIndex(archetype, *FindType(std::type_index(typeid(Components))))), ...);
    return columns;
  }

  template <typename... Components, typename Function, size_t... Indices>
  static void InvokeChunk(Function&& function, Chunk& chunk, const std::vector<size_t>& columns,
                          std::index_sequence<Indices...>) {
    std::invoke(
        std::forward<Function>(function), std::span<const EntityHandle>(chunk.Entities),
        std::span<Components>(static_cast<TypedColumn<Components>&>(*chunk.Columns[columns[Indices]]).Values)...);
  }

  template <typename... Components, typename Function, size_t... Indices>
  static void InvokeReadOnlyChunk(Function&& function, const Chunk& chunk, const std::vector<size_t>& columns,
                                  std::index_sequence<Indices...>) {
    std::invoke(std::forward<Function>(function), std::span<const EntityHandle>(chunk.Entities),
                std::span<const Components>(
                    static_cast<const TypedColumn<Components>&>(*chunk.Columns[columns[Indices]]).Values)...);
  }

  std::vector<ComponentDescriptor> m_Descriptors;
  std::unordered_map<std::type_index, ComponentTypeId> m_TypeIds;
  std::vector<Archetype> m_Archetypes;
  std::vector<EntityRecord> m_Entities;
  std::vector<uint32_t> m_FreeEntities;
  size_t m_AliveCount = 0;
  mutable size_t m_QueryDepth = 0;
  bool m_SystemPhase = false;
};

} // namespace Engine
