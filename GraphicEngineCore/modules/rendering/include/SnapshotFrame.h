#pragma once

#include "rendering/include/RenderOutput.h"
#include "runtime/include/CoreExport.h"
#include "runtime/include/Result.h"
#include "scene/include/SceneDocument.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // Snapshot frames own standard-library storage across the engine DLL boundary.
#endif

namespace Engine {

/// Stable identifiers for every plane owned by a complete snapshot frame.
/// The first five values match RenderAov; the integer planes remain typed UInt32 data.
enum class SnapshotPlane : uint8_t {
  FinalColor,         ///< Linear RGBA final color.
  Depth,              ///< Normalized device depth.
  WorldNormal,        ///< World-space shading normal.
  SpecularLighting,   ///< Specular lighting contribution.
  DiffuseLighting,    ///< Diffuse and emissive lighting contribution.
  MaterialInstanceId, ///< Exact UInt32 capture-local material-instance identity.
  Overdraw            ///< Exact UInt32 pre-depth fragment count.
};

/// Authored projection kind recorded with an immutable snapshot frame.
enum class SnapshotProjectionType : uint8_t { Perspective };

/// Captured color interpretation; values are never transformed by the frame container.
enum class SnapshotColorPrimaries : uint8_t { Rec709 };
enum class SnapshotTransferFunction : uint8_t { Linear };
enum class SnapshotLuminanceUnit : uint8_t { SceneLinearRelative, Nits };

/// Value-owned camera state needed to interpret a captured image.
struct ENGINE_API SnapshotCameraMetadata {
  std::string Identity;                                                    ///< Stable authored camera identity.
  std::array<float, 3> Position{};                                         ///< Captured world-space camera position.
  std::array<float, 4> Rotation{0.0F, 0.0F, 0.0F, 1.0F};                   ///< Captured world-space XYZW rotation.
  SnapshotProjectionType Projection = SnapshotProjectionType::Perspective; ///< Projection interpretation.
  float FieldOfViewDegrees = 45.0F;                                        ///< Vertical perspective field of view.
  float NearPlane = 0.1F;                                                  ///< Near clip distance.
  float FarPlane = 1000.0F;                                                ///< Far clip distance.
  uint32_t AuthoredWidth = 1920;  ///< Authored camera output width before a frame-local override.
  uint32_t AuthoredHeight = 1080; ///< Authored camera output height before a frame-local override.
};

/// Render settings that affect the captured final render.
struct ENGINE_API SnapshotRenderSettings {
  bool ShadowsEnabled = true;          ///< Whether scene shadowing was enabled.
  uint32_t ShadowMapResolution = 2048; ///< Shadow map resolution used for the capture.
  uint32_t MaxShadowCasters = 4;       ///< Maximum shadow-casting lights included.
  uint32_t SampleCount = 1;            ///< Capture samples per output pixel; snapshot captures require one.
};

/// Color interpretation and optional photometric calibration for captured linear RGB.
struct ENGINE_API SnapshotColorMetadata {
  SnapshotColorPrimaries Primaries = SnapshotColorPrimaries::Rec709;                ///< RGB primaries.
  SnapshotTransferFunction Transfer = SnapshotTransferFunction::Linear;             ///< Stored transfer function.
  SnapshotLuminanceUnit LuminanceUnit = SnapshotLuminanceUnit::SceneLinearRelative; ///< Luminance display unit.
  std::optional<float> NitsPerSceneUnit; ///< Calibration scale when luminance is in nits.
};

/// Value snapshot of all frame-level capture interpretation metadata.
struct ENGINE_API SnapshotFrameMetadata {
  std::string CaptureTimeUtc;            ///< Capture timestamp in ISO 8601 UTC form, for example 2026-10-19T12:00:00Z.
  std::string SceneIdentity;             ///< Stable identity of the source scene.
  SnapshotCameraMetadata Camera;         ///< Authored camera identity and captured projection state.
  uint32_t OutputWidth = 0;              ///< Captured output width, which must match every plane.
  uint32_t OutputHeight = 0;             ///< Captured output height, which must match every plane.
  SnapshotRenderSettings RenderSettings; ///< Value copy of render settings used for the capture.
  SnapshotColorMetadata Color;           ///< Color primaries, transfer, units, and optional calibration.
};

/// Capture-local mapping from an exact nonzero UInt32 plane value to captured material data.
struct ENGINE_API SnapshotMaterialManifestEntry {
  uint32_t Id = 0;                             ///< Capture-local material-instance ID; zero is reserved for background.
  std::string EntityIdentity;                  ///< Stable identity of the entity using this material instance.
  uint32_t SubmeshIndex = 0;                   ///< Entity submesh index.
  uint32_t MaterialSlot = 0;                   ///< Material slot assigned to the submesh.
  std::string MaterialIdentity;                ///< Stable material identity.
  SceneMaterialDocument CapturedValues;        ///< Material values as used by this capture.
  std::vector<std::string> ResourceIdentities; ///< Stable identities of captured material texture resources.
};

/// Immutable CPU-owned analysis frame. Construction validates all seven complete planes and
/// the material-ID manifest; published accessors expose only const views into owned storage.
class ENGINE_API SnapshotFrame {
public:
  SnapshotFrame(const SnapshotFrame&) = delete;
  SnapshotFrame& operator=(const SnapshotFrame&) = delete;
  SnapshotFrame(SnapshotFrame&&) noexcept = default;
  SnapshotFrame& operator=(SnapshotFrame&&) noexcept = delete;

  /// Validates and publishes one complete frame. No partial frame escapes validation failure.
  /// @param metadata Captured frame interpretation metadata.
  /// @param manifest Capture-local material-instance mapping.
  /// @param width Captured image width.
  /// @param height Captured image height.
  /// @param floatPlanes Completed five-AOV render planes in canonical order.
  /// @param materialInstanceIds Exact UInt32 material-instance ID samples.
  /// @param overdraw Exact UInt32 pre-depth fragment counts.
  /// @return Complete immutable frame, or a diagnostic when any input is invalid.
  static auto Create(SnapshotFrameMetadata metadata, std::vector<SnapshotMaterialManifestEntry> manifest,
                     uint32_t width, uint32_t height, std::vector<RenderOutputPlane> floatPlanes,
                     std::vector<uint32_t> materialInstanceIds, std::vector<uint32_t> overdraw)
      -> Result<SnapshotFrame>;
  /// Validates and publishes a complete frame using an existing final-render result.
  /// @param metadata Captured frame interpretation metadata.
  /// @param manifest Capture-local material-instance mapping.
  /// @param floatOutput Completed five-AOV final render.
  /// @param materialInstanceIds Exact UInt32 material-instance ID samples.
  /// @param overdraw Exact UInt32 pre-depth fragment counts.
  /// @return Complete immutable frame, or a diagnostic when any input is invalid.
  static auto Create(SnapshotFrameMetadata metadata, std::vector<SnapshotMaterialManifestEntry> manifest,
                     RenderOutput floatOutput, std::vector<uint32_t> materialInstanceIds,
                     std::vector<uint32_t> overdraw) -> Result<SnapshotFrame>;

  /// @return Captured width in pixels.
  uint32_t GetWidth() const noexcept { return m_FloatPlanes.Width; }
  /// @return Captured height in pixels.
  uint32_t GetHeight() const noexcept { return m_FloatPlanes.Height; }
  /// @return Read-only five-AOV float plane span.
  std::span<const RenderOutputPlane> GetFloatPlanes() const noexcept { return m_FloatPlanes.GetPlanes(); }
  /// @return Read-only exact UInt32 material-instance ID samples.
  std::span<const uint32_t> GetMaterialInstanceIds() const noexcept { return m_MaterialInstanceIds; }
  /// @return Read-only exact UInt32 overdraw counts.
  std::span<const uint32_t> GetOverdraw() const noexcept { return m_Overdraw; }
  /// @return Immutable captured frame metadata.
  const SnapshotFrameMetadata& GetMetadata() const noexcept { return m_Metadata; }
  /// @return Immutable capture-local material manifest.
  std::span<const SnapshotMaterialManifestEntry> GetManifest() const noexcept { return m_Manifest; }

private:
  SnapshotFrame(SnapshotFrameMetadata metadata, std::vector<SnapshotMaterialManifestEntry> manifest,
                RenderOutput floatPlanes, std::vector<uint32_t> materialInstanceIds,
                std::vector<uint32_t> overdraw) noexcept;

  SnapshotFrameMetadata m_Metadata;
  std::vector<SnapshotMaterialManifestEntry> m_Manifest;
  RenderOutput m_FloatPlanes;
  std::vector<uint32_t> m_MaterialInstanceIds;
  std::vector<uint32_t> m_Overdraw;
};

} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
