#pragma once

#include "rendering/include/RenderOutput.h"
#include "runtime/include/Result.h"

#include <filesystem>
#include <span>

namespace Engine::Rendering::Detail {

Status WritePngFile(uint32_t width, uint32_t height, std::span<const float> finalColorRgba,
                    const std::filesystem::path& path, const RenderOutputBackground& background = {});

Status WriteMultilayerExrFile(uint32_t width, uint32_t height, std::span<const RenderOutputPlane> planes,
                              std::span<const RenderAov> selectedAovs, const std::filesystem::path& path,
                              const RenderOutputBackground& background = {});

} // namespace Engine::Rendering::Detail
