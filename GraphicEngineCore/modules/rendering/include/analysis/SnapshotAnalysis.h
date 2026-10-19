#pragma once

#include "runtime/include/CoreExport.h"
#include "runtime/include/Result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Engine::SnapshotAnalysis {

/// Source-plane meaning used to validate analysis compatibility.
enum class PlaneSemantic : std::uint8_t {
  FinalColor,         ///< Linear scene-referred RGBA color.
  Depth,              ///< Normalized device depth.
  WorldNormal,        ///< World-space XYZ normal.
  SpecularLighting,   ///< Specular RGB contribution.
  DiffuseLighting,    ///< Diffuse RGB contribution.
  MaterialInstanceId, ///< Exact UInt32 capture-local identity.
  Overdraw            ///< Exact UInt32 eligible-fragment count.
};

/// Numeric storage format of a captured plane.
enum class ScalarFormat : std::uint8_t { Float32, UInt32 };

/// Color interpretation captured with a plane. Unknown values remain explicit;
/// comparisons require exact agreement and never guess a conversion.
enum class ColorPrimaries : std::uint8_t { NotApplicable, Rec709, Unknown };
enum class TransferFunction : std::uint8_t { NotApplicable, Linear, SRgb, Unknown };
enum class LuminanceUnit : std::uint8_t { NotApplicable, SceneLinearRelative, Nits };

/// Captured color and optional photometric interpretation for a plane.
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // Analysis results own standard-library storage across the engine DLL boundary.
#endif

struct ENGINE_API ColorInterpretation {
  ColorPrimaries Primaries = ColorPrimaries::NotApplicable;    ///< Captured color primaries.
  TransferFunction Transfer = TransferFunction::NotApplicable; ///< Captured transfer function.
  /// Optional nits represented by one scene-linear luminance unit; absent means relative units.
  std::optional<double> NitsPerSceneLinearUnit{};
};

/// Non-owning view over one tightly packed captured plane. Rows are top-to-bottom
/// and source pixel (x,y) is at y * Width + x. Only the span matching Format is used.
struct ENGINE_API PlaneView {
  std::uint32_t Width = 0;                            ///< Source width in pixels.
  std::uint32_t Height = 0;                           ///< Source height in pixels.
  std::uint8_t Channels = 0;                          ///< Interleaved channels per pixel.
  PlaneSemantic Semantic = PlaneSemantic::FinalColor; ///< Meaning of the samples.
  ScalarFormat Format = ScalarFormat::Float32;        ///< Numeric storage format.
  ColorInterpretation Color{};                        ///< Captured interpretation used for compatibility.
  std::span<const float> FloatSamples{};              ///< Tightly packed float samples when Format is Float32.
  std::span<const std::uint32_t> UIntSamples{};       ///< Tightly packed exact samples when Format is UInt32.
};

/// Half-open source-pixel rectangle [Left, Right) x [Top, Bottom).
struct ENGINE_API PixelRect {
  std::uint32_t Left = 0;   ///< Inclusive left source pixel.
  std::uint32_t Top = 0;    ///< Inclusive top source pixel.
  std::uint32_t Right = 0;  ///< Exclusive right source pixel.
  std::uint32_t Bottom = 0; ///< Exclusive bottom source pixel.
};

struct ENGINE_API RoiHistogramOptions {
  PixelRect Region{};         ///< Source-space half-open ROI.
  std::size_t BinCount = 256; ///< Number of bins per channel histogram.
  float Minimum = 0.0F;       ///< Lower histogram range; smaller samples enter the first bin.
  float Maximum = 1.0F;       ///< Upper histogram range; larger samples enter the last bin.
};

/// RGB and Rec. 709 luminance histograms over source pixels in Region. Values
/// below/above the requested range are accumulated in the end bins. Invalid
/// RGB samples are excluded and counted once per pixel. OutOfRangeValueCount
/// counts individual RGB/luminance values accumulated into an end bin. RGB
/// bins retain captured scene-linear channel values; luminance bins are scaled
/// to Nits only when NitsPerSceneLinearUnit is present.
struct ENGINE_API RoiHistogram {
  std::array<std::vector<std::uint64_t>, 4> Bins{};                  ///< R, G, B, luminance.
  std::uint64_t ValidPixelCount = 0;                                 ///< Pixels contributing to each histogram.
  std::uint64_t InvalidSampleCount = 0;                              ///< Pixels excluded due to non-finite RGB values.
  std::uint64_t OutOfRangeValueCount = 0;                            ///< Individual values accumulated into an end bin.
  float Minimum = 0.0F;                                              ///< Requested lower histogram range.
  float Maximum = 1.0F;                                              ///< Requested upper histogram range.
  LuminanceUnit LuminanceUnits = LuminanceUnit::SceneLinearRelative; ///< Units for luminance bins.
};

/// Computes RGB and linear Rec. 709 luminance histograms without modifying source data.
/// Nits are reported only when the captured photometric scale is present, finite, and positive.
/// @param plane Captured final-color plane view with linear Rec. 709 RGB values.
/// @param options Source-space ROI and histogram bin/range settings.
/// @return Four histograms (R, G, B, luminance), counts, and the applicable luminance unit.
[[nodiscard]] ENGINE_API Result<RoiHistogram> ComputeRoiHistogram(const PlaneView& plane,
                                                                  const RoiHistogramOptions& options);

enum class DifferenceMetric : std::uint8_t {
  MeanAbsolute,     ///< Mean of per-channel absolute errors, using RGB for final_color.
  RgbRms,           ///< RGB root-mean-square magnitude for final_color.
  RelativeLuminance ///< Relative Rec. 709 luminance error for linear final_color.
};

struct ENGINE_API DifferenceStatistics {
  std::uint64_t ValidSampleCount = 0;   ///< Number of pixels included in statistics.
  std::uint64_t InvalidSampleCount = 0; ///< Number of pixels excluded for non-finite float data.
  double Mean = 0.0;                    ///< Mean of valid per-pixel scalar values.
  double Maximum = 0.0;                 ///< Maximum valid per-pixel scalar value.
  double Rms = 0.0;                     ///< Root mean square of valid per-pixel scalar values.
};

/// Per-channel absolute differences are interleaved in source pixel order.
/// ScalarValues are unmodified metric results and are never heatmap-clamped.
struct ENGINE_API ComparisonResult {
  std::uint32_t Width = 0;                     ///< Source width in pixels.
  std::uint32_t Height = 0;                    ///< Source height in pixels.
  std::uint8_t Channels = 0;                   ///< Channels in the compared source plane.
  std::vector<double> ChannelAbsoluteErrors{}; ///< Interleaved abs(B-A) values, NaN for invalid pixels.
  std::vector<double> ScalarValues{};          ///< Unclamped per-pixel selected metric, NaN for invalid pixels.
  std::vector<std::uint8_t> ValidSamples{};    ///< One nonzero validity marker per source pixel.
  DifferenceStatistics Statistics{};           ///< Statistics computed from valid scalar values only.
};

/// Compares B against reference A, requiring identical dimensions, semantics,
/// channel count, scalar format, and color interpretation. Non-finite float
/// pixels are excluded and counted; integer samples remain exact.
/// @param referenceA Reference frame plane A.
/// @param comparisonB Comparison frame plane B; differences use B minus A direction.
/// @param metric Scalar difference metric for the display map and statistics.
/// @return Per-channel absolute errors, per-pixel metric values, validity mask, and statistics.
[[nodiscard]] ENGINE_API Result<ComparisonResult>
ComputeComparison(const PlaneView& referenceA, const PlaneView& comparisonB, DifferenceMetric metric);

/// Creates display-only normalized heatmap values clamped to [0,1]. Stored
/// scalar errors and statistics are not modified by this presentation range.
/// @param comparison Previously computed comparison values and validity mask.
/// @param minimum Lower display-range endpoint.
/// @param maximum Upper display-range endpoint, greater than minimum.
/// @return Normalized display values, retaining NaN for invalid pixels.
[[nodiscard]] ENGINE_API Result<std::vector<float>> MakeHeatmapDisplayValues(const ComparisonResult& comparison,
                                                                             double minimum, double maximum);

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

} // namespace Engine::SnapshotAnalysis
