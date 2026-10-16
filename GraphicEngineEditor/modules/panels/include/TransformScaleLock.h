#pragma once

#include <array>
#include <cstddef>

namespace Editor::TransformScaleLock {

/// Applies the scale inspector's optional uniform-scale behavior after a user edit.
/// @param scale Edited axis values.
/// @param previousScale Values before the current UI edit.
/// @param locked Whether the inspector's temporary scale lock is enabled.
/// @return True when the scale was changed by this helper.
inline auto Apply(std::array<float, 3>& scale, const std::array<float, 3>& previousScale, bool locked) noexcept
    -> bool {
  if (!locked) {
    return false;
  }
  for (std::size_t axis = 0; axis < scale.size(); ++axis) {
    if (scale[axis] != previousScale[axis]) {
      scale.fill(scale[axis]);
      return true;
    }
  }
  return false;
}

} // namespace Editor::TransformScaleLock
