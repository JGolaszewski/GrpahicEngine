#include "rendering/include/SnapshotFrame.h"

#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace Engine {
namespace {

constexpr std::array<RenderAov, 5> CanonicalAovs = {RenderAov::FinalColor, RenderAov::Depth, RenderAov::WorldNormal,
                                                    RenderAov::SpecularLighting, RenderAov::DiffuseLighting};

auto IsTimestampUtc(std::string_view value) -> bool {
  // Accept the stable whole-second form and ISO 8601 fractional seconds.
  if (value.size() < 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
      value[16] != ':' || value.back() != 'Z') {
    return false;
  }
  for (size_t index = 0; index < value.size(); ++index) {
    if (index == 4 || index == 7 || index == 10 || index == 13 || index == 16 || value[index] == 'Z') {
      continue;
    }
    if ((index == 19 && value[index] == '.') || (index > 19 && value[index] >= '0' && value[index] <= '9')) {
      continue;
    }
    if (value[index] < '0' || value[index] > '9') {
      return false;
    }
  }
  return value.size() == 20 || (value[19] == '.' && value.size() > 21);
}

auto IsFiniteMaterial(const SceneMaterialDocument& material) -> bool {
  const auto finiteArray = [](const auto& values) {
    return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
  };
  return finiteArray(material.baseColor) && finiteArray(material.emissive) && std::isfinite(material.metallic) &&
         std::isfinite(material.roughness) && std::isfinite(material.gtrGamma) && std::isfinite(material.ior) &&
         finiteArray(material.specularF0) && std::isfinite(material.alphaCutoff);
}

auto ValidateMetadata(const SnapshotFrameMetadata& metadata, uint32_t width, uint32_t height) -> Status {
  if (!IsTimestampUtc(metadata.CaptureTimeUtc)) {
    return std::unexpected(MakeError("snapshot capture time must be an ISO 8601 UTC timestamp"));
  }
  if (metadata.SceneIdentity.empty() || metadata.Camera.Identity.empty()) {
    return std::unexpected(MakeError("snapshot scene and camera identities are required"));
  }
  if (metadata.OutputWidth != width || metadata.OutputHeight != height || metadata.Camera.AuthoredWidth == 0 ||
      metadata.Camera.AuthoredHeight == 0) {
    return std::unexpected(MakeError("snapshot metadata dimensions do not match the captured planes"));
  }
  const auto finiteArray = [](const auto& values) {
    return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
  };
  float quaternionLengthSquared = 0.0F;
  for (float component : metadata.Camera.Rotation) {
    quaternionLengthSquared += component * component;
  }
  if (!finiteArray(metadata.Camera.Position) || !finiteArray(metadata.Camera.Rotation) ||
      !std::isfinite(metadata.Camera.FieldOfViewDegrees) || metadata.Camera.FieldOfViewDegrees <= 0.0F ||
      metadata.Camera.FieldOfViewDegrees >= 180.0F || !std::isfinite(metadata.Camera.NearPlane) ||
      !std::isfinite(metadata.Camera.FarPlane) || metadata.Camera.NearPlane <= 0.0F ||
      metadata.Camera.FarPlane <= metadata.Camera.NearPlane || quaternionLengthSquared < 1.0e-8F) {
    return std::unexpected(MakeError("snapshot camera metadata is invalid"));
  }
  if (metadata.RenderSettings.SampleCount != 1 || metadata.RenderSettings.ShadowMapResolution == 0) {
    return std::unexpected(MakeError("snapshot render settings are invalid"));
  }
  if (metadata.Camera.Projection != SnapshotProjectionType::Perspective ||
      metadata.Color.Primaries != SnapshotColorPrimaries::Rec709 ||
      metadata.Color.Transfer != SnapshotTransferFunction::Linear ||
      (metadata.Color.LuminanceUnit != SnapshotLuminanceUnit::SceneLinearRelative &&
       metadata.Color.LuminanceUnit != SnapshotLuminanceUnit::Nits)) {
    return std::unexpected(MakeError("snapshot interpretation metadata contains an unsupported value"));
  }
  const bool hasCalibration = metadata.Color.NitsPerSceneUnit.has_value();
  if ((metadata.Color.LuminanceUnit == SnapshotLuminanceUnit::Nits) != hasCalibration ||
      (hasCalibration &&
       (!std::isfinite(*metadata.Color.NitsPerSceneUnit) || *metadata.Color.NitsPerSceneUnit <= 0.0F))) {
    return std::unexpected(MakeError("snapshot photometric calibration does not match its luminance unit"));
  }
  return {};
}

auto ValidateFloatPlanes(uint32_t width, uint32_t height, std::span<const RenderOutputPlane> planes) -> Status {
  if (planes.size() != CanonicalAovs.size()) {
    return std::unexpected(MakeError("snapshot must contain all five canonical float AOVs"));
  }
  const uint64_t pixels = static_cast<uint64_t>(width) * height;
  for (size_t index = 0; index < CanonicalAovs.size(); ++index) {
    const auto& plane = planes[index];
    const auto& descriptor = RenderOutput::GetDescriptor(CanonicalAovs[index]);
    if (plane.Aov != CanonicalAovs[index] || plane.ChannelCount != descriptor.ChannelCount ||
        plane.Pixels.size() != pixels * descriptor.ChannelCount) {
      return std::unexpected(MakeError("snapshot float AOV layout is invalid or not in canonical order"));
    }
  }
  return {};
}

auto ValidateFloatOutput(const RenderOutput& output) -> Status {
  return ValidateFloatPlanes(output.Width, output.Height, output.GetPlanes());
}

auto ValidateManifest(std::span<const SnapshotMaterialManifestEntry> manifest, std::span<const uint32_t> materialIds)
    -> Status {
  std::unordered_set<uint32_t> knownIds;
  knownIds.reserve(manifest.size());
  for (const auto& entry : manifest) {
    if (entry.Id == 0 || entry.EntityIdentity.empty() || entry.MaterialIdentity.empty() ||
        !IsFiniteMaterial(entry.CapturedValues) || !knownIds.insert(entry.Id).second) {
      return std::unexpected(MakeError("snapshot material manifest contains an invalid or duplicate entry"));
    }
  }
  for (uint32_t id : materialIds) {
    if (id != 0 && !knownIds.contains(id)) {
      return std::unexpected(MakeError("snapshot material ID plane references an unmapped ID"));
    }
  }
  return {};
}

} // namespace

SnapshotFrame::SnapshotFrame(SnapshotFrameMetadata metadata, std::vector<SnapshotMaterialManifestEntry> manifest,
                             RenderOutput floatPlanes, std::vector<uint32_t> materialInstanceIds,
                             std::vector<uint32_t> overdraw) noexcept
    : m_Metadata(std::move(metadata)), m_Manifest(std::move(manifest)), m_FloatPlanes(std::move(floatPlanes)),
      m_MaterialInstanceIds(std::move(materialInstanceIds)), m_Overdraw(std::move(overdraw)) {}

auto SnapshotFrame::Create(SnapshotFrameMetadata metadata, std::vector<SnapshotMaterialManifestEntry> manifest,
                           uint32_t width, uint32_t height, std::vector<RenderOutputPlane> floatPlanes,
                           std::vector<uint32_t> materialInstanceIds, std::vector<uint32_t> overdraw)
    -> Result<SnapshotFrame> {
  if (width == 0 || height == 0 || static_cast<uint64_t>(width) * height > std::numeric_limits<size_t>::max()) {
    return std::unexpected(MakeError("snapshot dimensions must be nonzero and representable"));
  }
  const size_t pixelCount = static_cast<size_t>(width) * height;
  if (materialInstanceIds.size() != pixelCount || overdraw.size() != pixelCount) {
    return std::unexpected(MakeError("snapshot integer planes must match the captured dimensions"));
  }
  if (auto status = ValidateMetadata(metadata, width, height); !status) {
    return std::unexpected(status.error());
  }
  if (auto status = ValidateFloatPlanes(width, height, floatPlanes); !status) {
    return std::unexpected(status.error());
  }
  if (auto status = ValidateManifest(manifest, materialInstanceIds); !status) {
    return std::unexpected(status.error());
  }
  RenderOutput output;
  output.Width = width;
  output.Height = height;
  output.m_Planes = std::move(floatPlanes);
  return SnapshotFrame(std::move(metadata), std::move(manifest), std::move(output), std::move(materialInstanceIds),
                       std::move(overdraw));
}

auto SnapshotFrame::Create(SnapshotFrameMetadata metadata, std::vector<SnapshotMaterialManifestEntry> manifest,
                           RenderOutput floatOutput, std::vector<uint32_t> materialInstanceIds,
                           std::vector<uint32_t> overdraw) -> Result<SnapshotFrame> {
  const uint32_t width = floatOutput.Width;
  const uint32_t height = floatOutput.Height;
  if (width == 0 || height == 0 || static_cast<uint64_t>(width) * height > std::numeric_limits<size_t>::max()) {
    return std::unexpected(MakeError("snapshot dimensions must be nonzero and representable"));
  }
  const size_t pixelCount = static_cast<size_t>(width) * height;
  if (materialInstanceIds.size() != pixelCount || overdraw.size() != pixelCount) {
    return std::unexpected(MakeError("snapshot integer planes must match the captured dimensions"));
  }
  if (auto status = ValidateMetadata(metadata, width, height); !status) {
    return std::unexpected(status.error());
  }
  if (auto status = ValidateFloatOutput(floatOutput); !status) {
    return std::unexpected(status.error());
  }
  if (auto status = ValidateManifest(manifest, materialInstanceIds); !status) {
    return std::unexpected(status.error());
  }
  return SnapshotFrame(std::move(metadata), std::move(manifest), std::move(floatOutput), std::move(materialInstanceIds),
                       std::move(overdraw));
}

} // namespace Engine
