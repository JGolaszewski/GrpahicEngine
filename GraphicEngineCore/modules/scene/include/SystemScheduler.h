#pragma once

#include "scene/include/ArchetypeWorld.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace Engine {

/// Runs scene systems in deterministic registration order with access barriers.
///
/// Systems in a batch have no declared read/write conflicts. Execution remains
/// single-threaded; structural writers run alone and require synchronization.
class SystemScheduler {
public:
  /// Callback and declared component access for one scheduled system.
  struct System {
    std::string Name; ///< Unique name used for registration and diagnostics.
    std::vector<std::type_index> Reads; ///< Component types read by the callback.
    std::vector<std::type_index> Writes; ///< Component types written by the callback.
    bool WritesStructure = false; ///< Whether the callback requires exclusive structural synchronization.
    std::function<void(ArchetypeWorld&)> Execute; ///< Callback executed with world access.
  };

  /// Indices of systems that can share one scheduler batch.
  using Batch = std::vector<size_t>;
  /// Callback that applies structural changes between systems.
  using Synchronize = std::function<Status()>;

  /// Register a system, returning an error for invalid or duplicate names.
  /// @param system Name, component access declarations, and execution callback.
  /// @return Success, or an error when registration is invalid or execution is active.
  auto AddSystem(System system) -> Status {
    if (m_Executing) {
      return std::unexpected(MakeError("Systems cannot be registered during scheduler execution"));
    }
    if (system.Name.empty() || !system.Execute) {
      return std::unexpected(MakeError("A scheduled system requires a name and an execution callback"));
    }
    if (std::ranges::any_of(m_Systems,
                            [&system](const System& existing) -> bool { return existing.Name == system.Name; })) {
      return std::unexpected(MakeError("A system with this name is already registered"));
    }
    m_Systems.push_back(std::move(system));
    return {};
  }

  /// Builds deterministic batches from declared component access.
  /// @return System indices in registration order, grouped by conflict barriers.
  auto BuildBatches() const -> std::vector<Batch> {
    std::vector<Batch> batches;
    Batch current;
    for (size_t index = 0; index < m_Systems.size(); ++index) {
      const System& system = m_Systems[index];
      if (system.WritesStructure) {
        if (!current.empty()) {
          batches.push_back(std::move(current));
          current = {};
        }
        batches.push_back({index});
        continue;
      }
      const bool conflicts = std::ranges::any_of(current, [this, &system](size_t scheduledIndex) -> bool {
        return Conflicts(system, m_Systems[scheduledIndex]);
      });
      if (conflicts) {
        batches.push_back(std::move(current));
        current = {};
      }
      current.push_back(index);
    }
    if (!current.empty()) {
      batches.push_back(std::move(current));
    }
    return batches;
  }

  // Batches are emitted in registration order. Until a worker executor exists,
  // systems inside a batch also run serially in that order.
  /// Execute all registered systems and apply required synchronization barriers.
  /// @param world World passed to each system callback.
  /// @param synchronize Callback used after structural writers.
  /// @return Success, or an error from a system or synchronization callback.
  auto Execute(ArchetypeWorld& world, Synchronize synchronize = {}) const -> Status {
    if (!synchronize && std::ranges::any_of(m_Systems, [](const System& system) { return system.WritesStructure; })) {
      return std::unexpected(MakeError("Structural systems require a synchronization barrier"));
    }
    if (m_Executing) {
      return std::unexpected(MakeError("The scheduler cannot execute recursively"));
    }
    m_Executing = true;
    struct ExecutionReset {
      bool& IsExecuting;
      ~ExecutionReset() { IsExecuting = false; }
    } reset{m_Executing};
    try {
      for (const Batch& batch : BuildBatches()) {
        for (size_t index : batch) {
          const System& system = m_Systems[index];
          world.BeginSystemPhase();
          try {
            system.Execute(world);
          } catch (...) {
            world.EndSystemPhase();
            throw;
          }
          world.EndSystemPhase();
          if (system.WritesStructure) {
            const Status synchronized = synchronize();
            if (!synchronized) {
              return synchronized;
            }
          }
        }
      }
    } catch (const std::exception& error) {
      return std::unexpected(MakeError(error.what()));
    } catch (...) {
      return std::unexpected(MakeError("System execution failed"));
    }
    return {};
  }

  /// Checks whether two systems have conflicting component access.
  /// @param left First system access declaration.
  /// @param right Second system access declaration.
  /// @return True when either system writes a component read or written by the other.
  static auto Conflicts(const System& left, const System& right) -> bool {
    return Intersects(left.Writes, right.Reads) || Intersects(left.Writes, right.Writes) ||
           Intersects(right.Writes, left.Reads);
  }

private:
  static auto Intersects(const std::vector<std::type_index>& left, const std::vector<std::type_index>& right) -> bool {
    return std::ranges::any_of(
        left, [&right](const std::type_index& type) -> bool { return std::ranges::find(right, type) != right.end(); });
  }

  std::vector<System> m_Systems;
  mutable bool m_Executing = false;
};

} // namespace Engine
