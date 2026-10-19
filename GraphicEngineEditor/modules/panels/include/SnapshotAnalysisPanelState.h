#pragma once

#include "rendering/include/SnapshotFrame.h"
#include "scene/include/ArchetypeWorld.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

/// Camera entry shown by the Snapshot Analysis focused workspace.
struct SnapshotAnalysisCameraOption {
  Engine::EntityHandle Entity = Engine::InvalidEntityHandle; ///< Authored camera entity handle.
  std::string Label;                                         ///< User-facing camera name.
  bool IsCurrentSelection = false;                           ///< Whether this camera was selected on entry.
  bool IsActive = false;                                     ///< Whether the authored camera is active.
  int32_t Priority = 0;                                      ///< Authored active-camera priority.
};

/// Capture-plane UI initial state: all planes enabled.
inline constexpr Engine::SnapshotPlaneMask DefaultSnapshotCapturePlanes = Engine::AllSnapshotPlanes;

/// @return Whether at least one plane is selected for capture.
constexpr auto HasSnapshotCapturePlaneSelection(Engine::SnapshotPlaneMask mask) noexcept -> bool { return mask != 0U; }

/// @return Whether the capture mask contains the requested plane.
constexpr auto IsSnapshotCapturePlaneEnabled(Engine::SnapshotPlaneMask mask, Engine::SnapshotPlane plane) noexcept
    -> bool {
  return (mask & Engine::SnapshotPlaneMaskBit(plane)) != 0;
}

/// Updates one plane bit without changing other capture choices.
/// @param mask Current selected-plane mask.
/// @param plane Plane whose selection should change.
/// @param enabled Whether the plane should be selected.
/// @return Updated selected-plane mask.
constexpr auto SetSnapshotCapturePlaneEnabled(Engine::SnapshotPlaneMask mask, Engine::SnapshotPlane plane,
                                              bool enabled) noexcept -> Engine::SnapshotPlaneMask {
  const Engine::SnapshotPlaneMask bit = Engine::SnapshotPlaneMaskBit(plane);
  return enabled ? static_cast<Engine::SnapshotPlaneMask>(mask | bit)
                 : static_cast<Engine::SnapshotPlaneMask>(mask & static_cast<Engine::SnapshotPlaneMask>(~bit));
}

/// Returns available planes in the contract's stable canonical order.
/// @param frame Immutable frame to inspect.
/// @return Plane identifiers present in the frame.
inline auto GetAvailableSnapshotPlanes(const Engine::SnapshotFrame& frame) -> std::vector<Engine::SnapshotPlane> {
  std::vector<Engine::SnapshotPlane> available;
  available.reserve(7U);
  for (uint8_t index = 0; index < 7U; ++index) {
    const auto plane = static_cast<Engine::SnapshotPlane>(index);
    if (frame.HasPlane(plane)) {
      available.push_back(plane);
    }
  }
  return available;
}

/// Keeps a requested plane when available, otherwise chooses the first available plane.
/// @param frame Immutable frame to inspect.
/// @param requested Previously selected plane.
/// @return The requested plane, the first available plane, or no value for an empty frame.
inline auto SelectAvailableSnapshotPlane(const Engine::SnapshotFrame& frame, Engine::SnapshotPlane requested)
    -> std::optional<Engine::SnapshotPlane> {
  if (frame.HasPlane(requested)) {
    return requested;
  }
  const auto available = GetAvailableSnapshotPlanes(frame);
  return available.empty() ? std::nullopt : std::optional<Engine::SnapshotPlane>(available.front());
}

/// Chooses an initial camera deterministically: selected camera, highest-priority active camera, then lowest handle.
/// @param cameras Authored camera entries, in any order.
/// @return Preferred camera entity, or no value when there are no valid cameras.
inline auto SelectPreferredSnapshotCamera(std::span<const SnapshotAnalysisCameraOption> cameras)
    -> std::optional<Engine::EntityHandle> {
  const auto entityLess = [](const SnapshotAnalysisCameraOption& left, const SnapshotAnalysisCameraOption& right) {
    return left.Entity.Index < right.Entity.Index ||
           (left.Entity.Index == right.Entity.Index && left.Entity.Generation < right.Entity.Generation);
  };
  const SnapshotAnalysisCameraOption* current = nullptr;
  const SnapshotAnalysisCameraOption* active = nullptr;
  const SnapshotAnalysisCameraOption* first = nullptr;
  for (const auto& camera : cameras) {
    if (first == nullptr || entityLess(camera, *first)) {
      first = &camera;
    }
    if (camera.IsCurrentSelection && (current == nullptr || entityLess(camera, *current))) {
      current = &camera;
    }
    if (camera.IsActive && (active == nullptr || camera.Priority > active->Priority ||
                            (camera.Priority == active->Priority && entityLess(camera, *active)))) {
      active = &camera;
    }
  }
  if (current != nullptr) {
    return current->Entity;
  }
  if (active != nullptr) {
    return active->Entity;
  }
  return first == nullptr ? std::nullopt : std::optional<Engine::EntityHandle>(first->Entity);
}
