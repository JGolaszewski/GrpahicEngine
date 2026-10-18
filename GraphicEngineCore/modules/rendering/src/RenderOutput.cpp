#include "rendering/include/RenderOutput.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Engine {
namespace {
constexpr RenderAovDescriptor Descriptors[] = {
    {RenderAov::FinalColor, "final_color", RenderOutputFormat::Float32, 4, {"R", "G", "B", "A"}},
    {RenderAov::Depth, "depth", RenderOutputFormat::Float32, 1, {"Z", nullptr, nullptr, nullptr}},
    {RenderAov::WorldNormal, "world_normal", RenderOutputFormat::Float32, 3, {"X", "Y", "Z", nullptr}},
    {RenderAov::SpecularLighting, "specular_lighting", RenderOutputFormat::Float32, 3, {"R", "G", "B", nullptr}},
    {RenderAov::DiffuseLighting, "diffuse_lighting", RenderOutputFormat::Float32, 3, {"R", "G", "B", nullptr}},
};
} // namespace

auto RenderOutput::GetPixels(RenderAov aov) const noexcept -> std::span<const float> {
  const auto plane = std::ranges::find(m_Planes, aov, &RenderOutputPlane::Aov);
  return plane == m_Planes.end() ? std::span<const float>{} : std::span<const float>{plane->Pixels};
}

auto RenderOutput::GetDescriptor(RenderAov aov) noexcept -> const RenderAovDescriptor& {
  const auto descriptor = std::ranges::find(Descriptors, aov, &RenderAovDescriptor::Aov);
  return descriptor == std::end(Descriptors) ? Descriptors[0] : *descriptor;
}

auto RenderOutput::CreateFromPlanes(uint32_t width, uint32_t height, std::vector<RenderOutputPlane> planes)
    -> Result<RenderOutput> {
  if (width == 0 || height == 0) {
    return std::unexpected(MakeError("render output dimensions must be positive"));
  }
  if (planes.size() != std::size(Descriptors)) {
    return std::unexpected(MakeError("render output must contain all five canonical AOV planes"));
  }
  if (static_cast<size_t>(width) > (std::numeric_limits<size_t>::max)() / static_cast<size_t>(height)) {
    return std::unexpected(MakeError("render output dimensions exceed addressable storage"));
  }
  const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
  for (size_t index = 0; index < planes.size(); ++index) {
    const RenderAovDescriptor& descriptor = Descriptors[index];
    const RenderOutputPlane& plane = planes[index];
    if (plane.Aov != descriptor.Aov || plane.ChannelCount != descriptor.ChannelCount) {
      return std::unexpected(MakeError("render output AOV planes are not in canonical order or have invalid channels"));
    }
    if (pixelCount > (std::numeric_limits<size_t>::max)() / descriptor.ChannelCount ||
        plane.Pixels.size() != pixelCount * descriptor.ChannelCount) {
      return std::unexpected(MakeError("render output AOV plane sample count does not match its dimensions"));
    }
    if (!std::ranges::all_of(plane.Pixels, [](float sample) { return std::isfinite(sample); })) {
      return std::unexpected(MakeError("render output AOV plane contains a non-finite sample"));
    }
  }

  RenderOutput output;
  output.Width = width;
  output.Height = height;
  output.m_Planes = std::move(planes);
  return output;
}

} // namespace Engine
