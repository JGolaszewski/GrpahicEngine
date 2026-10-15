#pragma once

#include "runtime/include/CoreExport.h"
#include "runtime/include/Result.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // Output owns standard-library storage across the same-toolchain DLL boundary.
#endif

namespace Engine {

class RenderSystem;

/// Stable render-output plane identifiers used by previews and image exporters.
enum class RenderAov : uint8_t {
  FinalColor,
  Depth,
  WorldNormal,
  SpecularLighting,
  DiffuseLighting,
};

/// Scalar storage format used by all CPU render-output planes.
enum class RenderOutputFormat : uint8_t { Float32 };

/// Describes a render-output plane's stable name and channel layout.
struct ENGINE_API RenderAovDescriptor {
  RenderAov Aov;
  const char* Name;
  RenderOutputFormat Format;
  uint32_t ChannelCount;
  std::array<const char*, 4> ChannelNames;
};

/// CPU-owned, tightly packed float32 image plane.
struct ENGINE_API RenderOutputPlane {
  RenderAov Aov;
  uint32_t ChannelCount = 0;
  std::vector<float> Pixels;
};

/// Owning CPU result of a completed final render.
///
/// Every plane has Width * Height * ChannelCount float32 samples in row-major
/// top-to-bottom order. `final_color` is linear RGBA; `depth` is normalized
/// device depth in [0, 1] (1 for background); `world_normal` is the normalized
/// world-space shading normal; `specular_lighting` contains direct and
/// environment specular light; `diffuse_lighting` contains direct diffuse,
/// ambient/IBL diffuse, and emissive light. Before post-processing, final
/// color is the sum of diffuse and specular lighting in RGB, with material
/// opacity in alpha. Plane spans remain valid until this output is
/// destroyed or move-assigned. All five canonical planes are present on
/// success; failed renders return an error instead of partial or placeholder
/// data.
class ENGINE_API RenderOutput {
public:
  uint32_t Width = 0;
  uint32_t Height = 0;

  /// Returns the available AOV planes in canonical order.
  std::span<const RenderOutputPlane> GetPlanes() const noexcept { return m_Planes; }
  /// Returns the owned planes for a post-processing hook.
  std::span<RenderOutputPlane> GetMutablePlanes() noexcept { return m_Planes; }
  /// Returns the requested plane, or an empty span if this output is malformed.
  std::span<const float> GetPixels(RenderAov aov) const noexcept;
  /// Returns the stable descriptor for a canonical AOV identifier.
  static const RenderAovDescriptor& GetDescriptor(RenderAov aov) noexcept;

private:
  std::vector<RenderOutputPlane> m_Planes;
  friend class RenderSystem;
};

} // namespace Engine

namespace Engine {

/// CPU post-processing hook for final color, called after GPU readback.
///
/// The span contains tightly packed, top-to-bottom RGBA float32 samples. The
/// hook may modify final color in place; geometric and lighting AOVs remain
/// the renderer's unmodified outputs.
using FinalColorPostProcess = std::function<Status(uint32_t width, uint32_t height, std::span<float> finalColorRgba)>;

} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
