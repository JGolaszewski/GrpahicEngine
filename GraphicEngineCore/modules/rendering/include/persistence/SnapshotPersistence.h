#pragma once

#include "rendering/include/SnapshotFrame.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace Engine::SnapshotPersistence {

/// Writes or reads the complete, lossless Snapshot Analysis frame payload.
/// The EXR contains each float AOV, exact UINT32 material IDs and overdraw,
/// plus capture metadata and the material manifest.
class ENGINE_API SnapshotExr {
public:
  /// Atomically writes every frame plane and interpretation record to an EXR.
  /// @param frame Complete immutable frame to persist.
  /// @param path Destination with an .exr extension.
  /// @return Success or a diagnostic containing the destination path.
  static auto Write(const SnapshotFrame& frame, const std::filesystem::path& path) -> Status;
  /// Reads and validates every required channel and metadata record before publishing a frame.
  /// @param path EXR payload to read.
  /// @return Complete frame or a diagnostic containing the source path.
  static auto Read(const std::filesystem::path& path) -> Result<SnapshotFrame>;
};

/// Half-open source-image ROI stored in original pixel coordinates.
struct SnapshotRoiState {
  uint32_t X = 0;
  uint32_t Y = 0;
  uint32_t Width = 0;
  uint32_t Height = 0;
  auto operator==(const SnapshotRoiState&) const noexcept -> bool = default;
};

struct SnapshotPinnedPixelState {
  uint32_t X = 0;
  uint32_t Y = 0;
  auto operator==(const SnapshotPinnedPixelState&) const noexcept -> bool = default;
};

/// Per-frame inspector state. Zoom/pan only affects display and never pixel coordinates.
struct SnapshotFrameViewState {
  std::filesystem::path ExrPath; ///< Path relative to the .aframe document when saved.
  RenderAov SelectedAov = RenderAov::FinalColor;
  std::optional<SnapshotPinnedPixelState> PinnedPixel;
  std::optional<SnapshotRoiState> Roi;
  float Zoom = 1.0F;
  float PanX = 0.0F;
  float PanY = 0.0F;
};

enum class SnapshotComparisonMetric : uint8_t { AbsoluteError, RgbRms, RelativeLuminance };

struct SnapshotComparisonState {
  uint32_t ReferenceTab = 0;
  uint32_t CurrentTab = 0;
  SnapshotComparisonMetric Metric = SnapshotComparisonMetric::AbsoluteError;
  float WipePosition = 0.5F;
};

/// Versioned view-state document; frame pixels remain in separately referenced EXR payloads.
struct SnapshotWorkspaceDocument {
  static constexpr uint32_t CurrentVersion = 1;
  uint32_t Version = CurrentVersion;
  std::vector<SnapshotFrameViewState> Tabs;
  uint32_t SelectedTab = 0;
  std::optional<SnapshotComparisonState> Comparison;
};

/// Workspace with all payloads resolved and loaded before the caller swaps editor state.
struct SnapshotLoadedWorkspace {
  SnapshotWorkspaceDocument Document;
  std::vector<std::filesystem::path> ResolvedExrPaths;
  std::vector<SnapshotFrame> Frames;
};

/// Saves and opens versioned .aframe documents using relative EXR paths by default.
class ENGINE_API SnapshotWorkspaceFile {
public:
  /// Validates state and atomically writes a version 1 .aframe JSON document.
  /// @param path Destination with an .aframe extension.
  /// @param document Tabs and analysis view state to persist.
  /// @return Success or a diagnostic containing the destination path.
  static auto Save(const std::filesystem::path& path, const SnapshotWorkspaceDocument& document) -> Status;
  /// Parses state and loads every referenced EXR before returning any workspace data.
  /// @param path .aframe document to open.
  /// @return Fully loaded workspace or an error; caller state is never modified.
  static auto Load(const std::filesystem::path& path) -> Result<SnapshotLoadedWorkspace>;
};

} // namespace Engine::SnapshotPersistence
