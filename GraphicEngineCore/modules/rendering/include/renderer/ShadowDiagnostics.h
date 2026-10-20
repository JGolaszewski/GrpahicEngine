#pragma once

#include "runtime/include/CoreExport.h"
#include "scene/include/ArchetypeWorld.h"

#include <cstdint>

namespace Engine::Renderer {

/// Measurements for the directional shadow projection generated for one light.
struct ENGINE_API DirectionalShadowDiagnostics {
  Engine::EntityHandle Light = Engine::InvalidEntityHandle; ///< Scene light using this projection.
  uint32_t MapResolution = 0;                               ///< Shadow-map resolution in pixels.
  float CoverageWidth = 0.0F;                               ///< Square light-space receiver coverage in world units.
  float CoverageDepth = 0.0F;                               ///< Light-space receiver and caster depth in world units.
  float TexelWorldSize = 0.0F;                              ///< World-space size represented by one shadow-map texel.
  float Bias = 0.0F;                                        ///< Authored depth bias for this light.
};

} // namespace Engine::Renderer
