#include "analysis/SnapshotAnalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace Engine::SnapshotAnalysis {
namespace {

[[nodiscard]] bool HasExpectedSamples(const PlaneView& plane) {
  if (plane.Width == 0 || plane.Height == 0 || plane.Channels == 0) {
    return false;
  }
  const std::uint64_t pixelCount = static_cast<std::uint64_t>(plane.Width) * plane.Height;
  if (pixelCount > std::numeric_limits<std::size_t>::max() / plane.Channels) {
    return false;
  }
  const std::size_t count = static_cast<std::size_t>(pixelCount) * plane.Channels;
  if (plane.Format == ScalarFormat::Float32) {
    return plane.FloatSamples.size() == count;
  }
  return plane.UIntSamples.size() == count;
}

[[nodiscard]] Status ValidatePlane(const PlaneView& plane, const char* label) {
  if (plane.Width == 0 || plane.Height == 0) {
    return std::unexpected(MakeError(std::string(label) + " plane dimensions must be nonzero"));
  }
  if (plane.Channels == 0) {
    return std::unexpected(MakeError(std::string(label) + " plane must have at least one channel"));
  }
  const std::uint8_t expectedChannels = plane.Semantic == PlaneSemantic::FinalColor ? 4
                                        : plane.Semantic == PlaneSemantic::WorldNormal ||
                                                plane.Semantic == PlaneSemantic::SpecularLighting ||
                                                plane.Semantic == PlaneSemantic::DiffuseLighting
                                            ? 3
                                            : 1;
  if (plane.Channels != expectedChannels) {
    return std::unexpected(MakeError(std::string(label) + " plane channel count does not match its semantics"));
  }
  if (plane.Color.NitsPerSceneLinearUnit &&
      (!std::isfinite(*plane.Color.NitsPerSceneLinearUnit) || *plane.Color.NitsPerSceneLinearUnit <= 0.0)) {
    return std::unexpected(MakeError(std::string(label) + " plane photometric scale must be finite and positive"));
  }
  const bool requiresUInt32 =
      plane.Semantic == PlaneSemantic::MaterialInstanceId || plane.Semantic == PlaneSemantic::Overdraw;
  if ((requiresUInt32 && plane.Format != ScalarFormat::UInt32) ||
      (!requiresUInt32 && plane.Format != ScalarFormat::Float32)) {
    return std::unexpected(MakeError(std::string(label) + " plane scalar format does not match its semantics"));
  }
  if (!HasExpectedSamples(plane)) {
    return std::unexpected(MakeError(std::string(label) + " plane sample storage does not match its layout"));
  }
  return {};
}

[[nodiscard]] std::size_t HistogramBin(double value, double minimum, double maximum, std::size_t binCount,
                                       bool& isOutOfRange) {
  isOutOfRange = value < minimum || value > maximum;
  if (value <= minimum) {
    return 0;
  }
  if (value >= maximum) {
    return binCount - 1;
  }
  const double normalized = (value - minimum) / (maximum - minimum);
  return std::min(static_cast<std::size_t>(normalized * static_cast<double>(binCount)), binCount - 1);
}

[[nodiscard]] bool IsRgbColor(const PlaneView& plane) {
  return plane.Semantic == PlaneSemantic::FinalColor && plane.Channels >= 3;
}

[[nodiscard]] double Luminance(double red, double green, double blue) {
  return 0.2126 * red + 0.7152 * green + 0.0722 * blue;
}

} // namespace

Result<RoiHistogram> ComputeRoiHistogram(const PlaneView& plane, const RoiHistogramOptions& options) {
  if (auto status = ValidatePlane(plane, "ROI source"); !status) {
    return std::unexpected(status.error());
  }
  if (plane.Format != ScalarFormat::Float32 || plane.Semantic != PlaneSemantic::FinalColor || plane.Channels < 3) {
    return std::unexpected(
        MakeError("ROI RGB/luminance histogram requires a Float32 final_color plane with RGB channels"));
  }
  if (plane.Color.Transfer != TransferFunction::Linear || plane.Color.Primaries != ColorPrimaries::Rec709) {
    return std::unexpected(MakeError("ROI luminance requires linear Rec. 709 color interpretation"));
  }
  if (options.Region.Left >= options.Region.Right || options.Region.Top >= options.Region.Bottom ||
      options.Region.Right > plane.Width || options.Region.Bottom > plane.Height) {
    return std::unexpected(MakeError("ROI must be a nonempty source-space rectangle inside the plane"));
  }
  if (options.BinCount == 0 || options.BinCount > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected(MakeError("ROI histogram bin count must be in [1, UINT32_MAX]"));
  }
  if (!std::isfinite(options.Minimum) || !std::isfinite(options.Maximum) || options.Maximum <= options.Minimum) {
    return std::unexpected(MakeError("ROI histogram range must be finite with maximum greater than minimum"));
  }

  RoiHistogram result{};
  result.Minimum = options.Minimum;
  result.Maximum = options.Maximum;
  result.LuminanceUnits = plane.Color.NitsPerSceneLinearUnit ? LuminanceUnit::Nits : LuminanceUnit::SceneLinearRelative;
  for (auto& bins : result.Bins) {
    bins.assign(options.BinCount, 0);
  }

  for (std::uint32_t y = options.Region.Top; y < options.Region.Bottom; ++y) {
    for (std::uint32_t x = options.Region.Left; x < options.Region.Right; ++x) {
      const std::size_t pixelIndex = static_cast<std::size_t>(y) * plane.Width + x;
      const std::size_t sampleIndex = pixelIndex * plane.Channels;
      const double red = plane.FloatSamples[sampleIndex];
      const double green = plane.FloatSamples[sampleIndex + 1];
      const double blue = plane.FloatSamples[sampleIndex + 2];
      if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue)) {
        ++result.InvalidSampleCount;
        continue;
      }

      ++result.ValidPixelCount;
      const double luminance = Luminance(red, green, blue) * plane.Color.NitsPerSceneLinearUnit.value_or(1.0);
      const std::array<double, 4> values{red, green, blue, luminance};
      for (std::size_t channel = 0; channel < values.size(); ++channel) {
        bool outOfRange = false;
        const std::size_t bin =
            HistogramBin(values[channel], options.Minimum, options.Maximum, options.BinCount, outOfRange);
        ++result.Bins[channel][bin];
        result.OutOfRangeValueCount += outOfRange ? 1U : 0U;
      }
    }
  }
  return result;
}

Result<ComparisonResult> ComputeComparison(const PlaneView& referenceA, const PlaneView& comparisonB,
                                           DifferenceMetric metric) {
  if (auto status = ValidatePlane(referenceA, "Reference A"); !status) {
    return std::unexpected(status.error());
  }
  if (auto status = ValidatePlane(comparisonB, "Comparison B"); !status) {
    return std::unexpected(status.error());
  }
  if (referenceA.Width != comparisonB.Width || referenceA.Height != comparisonB.Height) {
    return std::unexpected(MakeError("Comparison requires equal plane dimensions; resampling is not supported"));
  }
  if (referenceA.Semantic != comparisonB.Semantic) {
    return std::unexpected(MakeError("Comparison plane semantics do not match"));
  }
  if (referenceA.Channels != comparisonB.Channels) {
    return std::unexpected(MakeError("Comparison channel layouts do not match"));
  }
  if (referenceA.Format != comparisonB.Format) {
    return std::unexpected(MakeError("Comparison scalar formats do not match"));
  }
  if (referenceA.Color.Primaries != comparisonB.Color.Primaries ||
      referenceA.Color.Transfer != comparisonB.Color.Transfer ||
      referenceA.Color.NitsPerSceneLinearUnit != comparisonB.Color.NitsPerSceneLinearUnit) {
    return std::unexpected(MakeError("Comparison color interpretation or units do not match"));
  }

  if (metric == DifferenceMetric::RgbRms && (referenceA.Format != ScalarFormat::Float32 || !IsRgbColor(referenceA))) {
    return std::unexpected(MakeError("RGB RMS requires a Float32 final_color plane with RGB channels"));
  }
  if (metric == DifferenceMetric::RelativeLuminance &&
      (referenceA.Format != ScalarFormat::Float32 || !IsRgbColor(referenceA) ||
       referenceA.Color.Transfer != TransferFunction::Linear || referenceA.Color.Primaries != ColorPrimaries::Rec709)) {
    return std::unexpected(
        MakeError("Relative luminance requires compatible linear Rec. 709 Float32 final_color planes"));
  }

  const std::size_t pixelCount = static_cast<std::size_t>(referenceA.Width) * referenceA.Height;
  ComparisonResult result{};
  result.Width = referenceA.Width;
  result.Height = referenceA.Height;
  result.Channels = referenceA.Channels;
  result.ChannelAbsoluteErrors.resize(pixelCount * referenceA.Channels, std::numeric_limits<double>::quiet_NaN());
  result.ScalarValues.resize(pixelCount, std::numeric_limits<double>::quiet_NaN());
  result.ValidSamples.resize(pixelCount, 0);
  long double sum = 0.0L;
  long double sumSquares = 0.0L;

  for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
    const std::size_t base = pixel * referenceA.Channels;
    bool valid = true;
    if (referenceA.Format == ScalarFormat::Float32) {
      for (std::uint8_t channel = 0; channel < referenceA.Channels; ++channel) {
        valid = valid && std::isfinite(referenceA.FloatSamples[base + channel]) &&
                std::isfinite(comparisonB.FloatSamples[base + channel]);
      }
    }
    if (!valid) {
      ++result.Statistics.InvalidSampleCount;
      continue;
    }

    for (std::uint8_t channel = 0; channel < referenceA.Channels; ++channel) {
      const double valueA = referenceA.Format == ScalarFormat::Float32
                                ? static_cast<double>(referenceA.FloatSamples[base + channel])
                                : static_cast<double>(referenceA.UIntSamples[base + channel]);
      const double valueB = referenceA.Format == ScalarFormat::Float32
                                ? static_cast<double>(comparisonB.FloatSamples[base + channel])
                                : static_cast<double>(comparisonB.UIntSamples[base + channel]);
      result.ChannelAbsoluteErrors[base + channel] = std::abs(valueB - valueA);
    }

    const std::uint8_t metricChannels = IsRgbColor(referenceA) ? 3 : referenceA.Channels;
    double scalar = 0.0;
    if (metric == DifferenceMetric::RelativeLuminance) {
      const double yA = Luminance(referenceA.FloatSamples[base], referenceA.FloatSamples[base + 1],
                                  referenceA.FloatSamples[base + 2]);
      const double yB = Luminance(comparisonB.FloatSamples[base], comparisonB.FloatSamples[base + 1],
                                  comparisonB.FloatSamples[base + 2]);
      scalar = std::abs(yB - yA) / std::max(std::abs(yA), 1.0e-6);
    } else if (metric == DifferenceMetric::RgbRms) {
      double squared = 0.0;
      for (std::uint8_t channel = 0; channel < 3; ++channel) {
        const double difference = result.ChannelAbsoluteErrors[base + channel];
        squared += difference * difference;
      }
      scalar = std::sqrt(squared / 3.0);
    } else {
      for (std::uint8_t channel = 0; channel < metricChannels; ++channel) {
        scalar += result.ChannelAbsoluteErrors[base + channel];
      }
      scalar /= metricChannels;
    }

    result.ScalarValues[pixel] = scalar;
    result.ValidSamples[pixel] = 1;
    ++result.Statistics.ValidSampleCount;
    sum += scalar;
    sumSquares += static_cast<long double>(scalar) * scalar;
    result.Statistics.Maximum = std::max(result.Statistics.Maximum, scalar);
  }

  if (result.Statistics.ValidSampleCount != 0) {
    const long double count = static_cast<long double>(result.Statistics.ValidSampleCount);
    result.Statistics.Mean = static_cast<double>(sum / count);
    result.Statistics.Rms = static_cast<double>(std::sqrt(sumSquares / count));
  }
  return result;
}

Result<std::vector<float>> MakeHeatmapDisplayValues(const ComparisonResult& comparison, double minimum,
                                                    double maximum) {
  if (comparison.ScalarValues.size() != comparison.ValidSamples.size()) {
    return std::unexpected(MakeError("Comparison scalar values and validity mask have different lengths"));
  }
  if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum) {
    return std::unexpected(MakeError("Heatmap display range must be finite with maximum greater than minimum"));
  }
  std::vector<float> result(comparison.ScalarValues.size(), std::numeric_limits<float>::quiet_NaN());
  for (std::size_t i = 0; i < comparison.ScalarValues.size(); ++i) {
    if (comparison.ValidSamples[i] == 0) {
      continue;
    }
    const double normalized = (comparison.ScalarValues[i] - minimum) / (maximum - minimum);
    result[i] = static_cast<float>(std::clamp(normalized, 0.0, 1.0));
  }
  return result;
}

} // namespace Engine::SnapshotAnalysis
