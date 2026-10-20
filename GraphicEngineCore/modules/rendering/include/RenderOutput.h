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
class SnapshotFrame;

/// Stable float render-output plane identifiers used by previews and image exporters.
///
/// These five float AOVs form the transient final-render result. Snapshot
/// Analysis adds typed integer planes and captured metadata in its immutable
/// frame representation; see the Snapshot Analysis workspace documentation.
enum class RenderAov : uint8_t {
  FinalColor,       ///< Post-processed color and opacity.
  Depth,            ///< Normalized device depth.
  WorldNormal,      ///< World-space shading normal.
  SpecularLighting, ///< Direct and environment specular contribution.
  DiffuseLighting,  ///< Direct diffuse, ambient diffuse, and emissive contribution.
};

/// Scalar storage format used by all CPU render-output planes.
enum class RenderOutputFormat : uint8_t {
  Float32 ///< 32-bit floating-point samples.
};

/// Describes a render-output plane's stable name and channel layout.
struct ENGINE_API RenderAovDescriptor {
  RenderAov Aov;                           ///< Stable plane identifier.
  const char* Name;                        ///< Stable lower-case plane name.
  RenderOutputFormat Format;               ///< Scalar storage format.
  uint32_t ChannelCount;                   ///< Number of scalar values per pixel.
  std::array<const char*, 4> ChannelNames; ///< Names of populated channels, in storage order.
};

/// CPU-owned, tightly packed float32 image plane.
struct ENGINE_API RenderOutputPlane {
  RenderAov Aov;             ///< Stable plane identifier.
  uint32_t ChannelCount = 0; ///< Number of scalar values per pixel.
  std::vector<float> Pixels; ///< Tightly packed top-to-bottom pixel samples.
};

/// Owning CPU result of a completed final render, not a persistent analysis frame.
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
/// data. Snapshot Analysis copies the completed output into its own frame only
/// after all additional typed planes and capture metadata are also complete.
class ENGINE_API RenderOutput {
public:
  uint32_t Width = 0;  ///< Image width in pixels.
  uint32_t Height = 0; ///< Image height in pixels.

  /// Returns the available AOV planes in canonical order.
  /// @return Read-only plane span, valid until destruction or move assignment.
  std::span<const RenderOutputPlane> GetPlanes() const noexcept { return m_Planes; }
  /// Returns the owned planes for a post-processing hook.
  /// @return Mutable plane span, valid until destruction or move assignment.
  std::span<RenderOutputPlane> GetMutablePlanes() noexcept { return m_Planes; }
  /// Returns the requested plane, or an empty span if this output is malformed.
  /// @param aov Stable plane identifier to look up.
  /// @return Tightly packed plane samples, or an empty span when unavailable.
  std::span<const float> GetPixels(RenderAov aov) const noexcept;
  /// Returns the stable descriptor for a canonical AOV identifier.
  /// @param aov Stable plane identifier to describe.
  /// @return Static descriptor for the requested plane.
  static const RenderAovDescriptor& GetDescriptor(RenderAov aov) noexcept;

  /// Rebuilds a complete CPU render output from canonical serialized planes.
  ///
  /// The factory accepts exactly the five canonical planes in descriptor order,
  /// with exact tightly packed dimensions and finite float samples. It is
  /// suitable for restoring captured output without a renderer or GPU device.
  /// @param width Image width in pixels.
  /// @param height Image height in pixels.
  /// @param planes Owned planes in canonical AOV order.
  /// @return A complete output, or an error when dimensions or plane data are invalid.
  static auto CreateFromPlanes(uint32_t width, uint32_t height, std::vector<RenderOutputPlane> planes)
      -> Result<RenderOutput>;

private:
  std::vector<RenderOutputPlane> m_Planes;
  friend class RenderSystem;
  friend class SnapshotFrame;
};

} // namespace Engine

namespace Engine {

/// Optional linear-RGB solid background for image export. When enabled, only
/// final_color is composited and its alpha is set to one.
struct ENGINE_API RenderOutputBackground {
  bool Enabled = false;                         ///< Whether to composite transparent final_color pixels.
  std::array<float, 3> Color{0.0F, 0.0F, 0.0F}; ///< Solid background color in linear RGB.
};

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
