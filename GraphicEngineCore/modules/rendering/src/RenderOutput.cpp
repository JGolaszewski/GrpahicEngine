#include "rendering/include/RenderOutput.h"

#include <algorithm>

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

} // namespace Engine
