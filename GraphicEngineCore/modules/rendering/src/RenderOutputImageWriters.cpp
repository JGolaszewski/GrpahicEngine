#include "RenderOutputExporterInternal.h"

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4702) // OpenImageIO's template code triggers MSVC's unreachable-code warning.
#endif
#include <OpenImageIO/imageio.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Engine::Rendering::Detail {
namespace {

auto ValidDimensions(uint32_t width, uint32_t height) -> bool {
  return width > 0 && height > 0 && width <= static_cast<uint32_t>(std::numeric_limits<int>::max()) &&
         height <= static_cast<uint32_t>(std::numeric_limits<int>::max());
}

auto CheckedSampleCount(uint32_t width, uint32_t height, uint32_t channels) -> std::optional<size_t> {
  const uint64_t sampleCount = static_cast<uint64_t>(width) * height * channels;
  if (sampleCount > std::numeric_limits<size_t>::max()) {
    return std::nullopt;
  }
  return static_cast<size_t>(sampleCount);
}

auto PngExtension(const std::filesystem::path& path) -> bool {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return extension == ".png";
}

auto ExrExtension(const std::filesystem::path& path) -> bool {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return extension == ".exr";
}

auto FormatFor(const std::filesystem::path& path) -> std::string {
  auto extension = path.extension().string();
  if (extension.size() <= 1) {
    return {};
  }
  extension.erase(extension.begin());
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return extension;
}

auto IoError(std::string_view operation, const std::filesystem::path& path, const std::string& detail) -> Error {
  std::string message(operation);
  message.append(" '");
  message.append(path.string());
  message.append("'");
  if (!detail.empty()) {
    message.append(": ");
    message.append(detail);
  }
  return MakeError(std::move(message));
}

auto LinearToSrgb(float linear) -> float {
  const float nonnegative = std::max(linear, 0.0F);
  const float reinhard = nonnegative / (1.0F + nonnegative);
  if (reinhard <= 0.0031308F) {
    return 12.92F * reinhard;
  }
  return 1.055F * std::pow(reinhard, 1.0F / 2.4F) - 0.055F;
}

auto ToByte(float value) -> uint8_t {
  const float clamped = std::clamp(value, 0.0F, 1.0F);
  return static_cast<uint8_t>(std::lround(clamped * 255.0F));
}

auto FindPlane(std::span<const RenderOutputPlane> planes, RenderAov aov) -> const RenderOutputPlane* {
  for (const auto& plane : planes) {
    if (plane.Aov == aov) {
      return &plane;
    }
  }
  return nullptr;
}

auto IsCanonicalAov(RenderAov aov) -> bool {
  switch (aov) {
  case RenderAov::FinalColor:
  case RenderAov::Depth:
  case RenderAov::WorldNormal:
  case RenderAov::SpecularLighting:
  case RenderAov::DiffuseLighting:
    return true;
  }
  return false;
}

auto ValidatePlane(uint32_t width, uint32_t height, const RenderOutputPlane& plane,
                   const RenderAovDescriptor& descriptor, const std::filesystem::path& path) -> Status {
  if (!IsCanonicalAov(plane.Aov) || plane.Aov != descriptor.Aov) {
    return std::unexpected(IoError("Cannot export invalid AOV", path, "AOV identifier does not match descriptor"));
  }
  if (descriptor.Format != RenderOutputFormat::Float32 || plane.ChannelCount != descriptor.ChannelCount ||
      descriptor.ChannelCount == 0 || descriptor.ChannelCount > descriptor.ChannelNames.size()) {
    return std::unexpected(IoError("Cannot export AOV", path, "unsupported AOV channel format"));
  }
  const auto expectedSamples = CheckedSampleCount(width, height, descriptor.ChannelCount);
  if (!expectedSamples || plane.Pixels.size() != *expectedSamples) {
    return std::unexpected(IoError("Cannot export AOV", path, "AOV pixel count does not match its dimensions"));
  }
  for (uint32_t channel = 0; channel < descriptor.ChannelCount; ++channel) {
    if (descriptor.ChannelNames[channel] == nullptr || descriptor.ChannelNames[channel][0] == '\0') {
      return std::unexpected(IoError("Cannot export AOV", path, "AOV descriptor has an unnamed channel"));
    }
  }
  return {};
}

auto MakeExrSpec(uint32_t width, uint32_t height, const RenderAovDescriptor& descriptor) -> OIIO::ImageSpec {
  OIIO::ImageSpec spec(static_cast<int>(width), static_cast<int>(height), static_cast<int>(descriptor.ChannelCount),
                       OIIO::TypeDesc::FLOAT);
  spec.channelnames.clear();
  for (uint32_t channel = 0; channel < descriptor.ChannelCount; ++channel) {
    spec.channelnames.emplace_back(descriptor.ChannelNames[channel]);
  }
  spec.attribute("oiio:subimagename", descriptor.Name);
  spec.attribute("engine:aov", descriptor.Name);
  spec.attribute("engine:aov_format", "float32");
  if (descriptor.Aov == RenderAov::Depth) {
    spec.attribute("engine:depth_semantics", "normalized_device_depth [0,1], background=1");
  }
  return spec;
}

} // namespace

auto WritePngFile(uint32_t width, uint32_t height, std::span<const float> finalColorRgba,
                  const std::filesystem::path& path) -> Status {
  if (!ValidDimensions(width, height)) {
    return std::unexpected(IoError("Cannot write PNG", path, "image dimensions must be non-zero and supported"));
  }
  if (path.empty() || !PngExtension(path)) {
    return std::unexpected(IoError("Cannot write PNG", path, "destination must have a .png extension"));
  }
  const auto expectedSamples = CheckedSampleCount(width, height, 4);
  if (!expectedSamples || finalColorRgba.size() != *expectedSamples) {
    return std::unexpected(IoError("Cannot write PNG", path, "final_color must contain width * height * 4 samples"));
  }

  std::vector<uint8_t> pixels(*expectedSamples);
  for (size_t pixel = 0; pixel < finalColorRgba.size() / 4; ++pixel) {
    const size_t offset = pixel * 4;
    for (size_t channel = 0; channel < 3; ++channel) {
      const float sample = finalColorRgba[offset + channel];
      if (!std::isfinite(sample)) {
        return std::unexpected(IoError("Cannot write PNG", path, "final_color contains a non-finite RGB sample"));
      }
      pixels[offset + channel] = ToByte(LinearToSrgb(sample));
    }
    const float alpha = finalColorRgba[offset + 3];
    if (!std::isfinite(alpha)) {
      return std::unexpected(IoError("Cannot write PNG", path, "final_color contains a non-finite alpha sample"));
    }
    pixels[offset + 3] = ToByte(alpha);
  }

  auto output = OIIO::ImageOutput::create(FormatFor(path));
  if (!output) {
    return std::unexpected(IoError("Cannot create PNG encoder for", path, OIIO::geterror()));
  }
  OIIO::ImageSpec spec(static_cast<int>(width), static_cast<int>(height), 4, OIIO::TypeDesc::UINT8);
  spec.channelnames = {"R", "G", "B", "A"};
  spec.alpha_channel = 3;
  spec.attribute("oiio:ColorSpace", "sRGB");
  if (!output->open(path.wstring(), spec)) {
    return std::unexpected(IoError("Cannot open PNG destination", path, output->geterror()));
  }
  if (!output->write_image(OIIO::span<const uint8_t>(pixels.data(), pixels.size()))) {
    const auto detail = output->geterror();
    (void)output->close();
    return std::unexpected(IoError("Cannot write PNG", path, detail));
  }
  if (!output->close()) {
    return std::unexpected(IoError("Cannot close PNG", path, output->geterror()));
  }
  return {};
}

auto WriteMultilayerExrFile(uint32_t width, uint32_t height, std::span<const RenderOutputPlane> planes,
                            std::span<const RenderAov> selectedAovs, const std::filesystem::path& path) -> Status {
  if (!ValidDimensions(width, height)) {
    return std::unexpected(IoError("Cannot write EXR", path, "image dimensions must be non-zero and supported"));
  }
  if (path.empty() || !ExrExtension(path)) {
    return std::unexpected(IoError("Cannot write EXR", path, "destination must have an .exr extension"));
  }
  if (selectedAovs.empty()) {
    return std::unexpected(IoError("Cannot write EXR", path, "select at least one AOV"));
  }

  std::vector<const RenderOutputPlane*> selectedPlanes;
  std::vector<RenderAov> seenAovs;
  std::vector<OIIO::ImageSpec> specs;
  selectedPlanes.reserve(selectedAovs.size());
  seenAovs.reserve(selectedAovs.size());
  specs.reserve(selectedAovs.size());
  for (const RenderAov aov : selectedAovs) {
    if (!IsCanonicalAov(aov)) {
      return std::unexpected(IoError("Cannot write EXR", path, "selection contains an unknown AOV"));
    }
    if (std::find(seenAovs.begin(), seenAovs.end(), aov) != seenAovs.end()) {
      return std::unexpected(IoError("Cannot write EXR", path, "selection contains a duplicate AOV"));
    }
    seenAovs.push_back(aov);
    const auto* plane = FindPlane(planes, aov);
    if (plane == nullptr) {
      const auto& descriptor = RenderOutput::GetDescriptor(aov);
      return std::unexpected(
          IoError("Cannot write EXR", path, std::string("selected AOV is unavailable: ") + descriptor.Name));
    }
    const auto& descriptor = RenderOutput::GetDescriptor(aov);
    if (auto status = ValidatePlane(width, height, *plane, descriptor, path); !status) {
      return status;
    }
    selectedPlanes.push_back(plane);
    specs.push_back(MakeExrSpec(width, height, descriptor));
  }

  auto output = OIIO::ImageOutput::create(FormatFor(path));
  if (!output) {
    return std::unexpected(IoError("Cannot create EXR encoder for", path, OIIO::geterror()));
  }
  if (output->supports("multiimage") == 0) {
    return std::unexpected(
        IoError("Cannot write EXR", path, "OpenImageIO EXR plugin does not support multipart output"));
  }

  // The UTF-16 multi-image overload falls back to a single-image open in OIIO,
  // so use the UTF-8 virtual overload to predeclare every EXR part.
  const auto filename = path.string();
  if (!output->open(filename, static_cast<int>(specs.size()), specs.data())) {
    return std::unexpected(IoError("Cannot open EXR destination", path, output->geterror()));
  }

  for (size_t index = 0; index < selectedPlanes.size(); ++index) {
    if (index > 0 && !output->open(filename, specs[index], OIIO::ImageOutput::AppendSubimage)) {
      const auto detail = output->geterror();
      (void)output->close();
      return std::unexpected(IoError("Cannot append EXR part", path, detail));
    }
    const auto& samples = selectedPlanes[index]->Pixels;
    if (!output->write_image(OIIO::span<const float>(samples.data(), samples.size()))) {
      const auto detail = output->geterror();
      (void)output->close();
      return std::unexpected(IoError("Cannot write EXR part", path, detail));
    }
  }
  if (!output->close()) {
    return std::unexpected(IoError("Cannot close EXR", path, output->geterror()));
  }
  return {};
}

} // namespace Engine::Rendering::Detail
