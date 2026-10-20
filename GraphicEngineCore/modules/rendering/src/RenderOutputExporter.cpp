#include "rendering/include/RenderOutputExporter.h"

#include "RenderOutputExporterInternal.h"

#include <algorithm>

namespace Engine {

auto RenderOutputExporter::WritePng(const RenderOutput& output, const std::filesystem::path& path,
                                    const RenderOutputBackground& background) -> Status {
  const auto finalColor = output.GetPixels(RenderAov::FinalColor);
  return Rendering::Detail::WritePngFile(output.Width, output.Height, finalColor, path, background);
}

auto RenderOutputExporter::WritePngRgba8(uint32_t width, uint32_t height, std::span<const uint8_t> rgba,
                                         const std::filesystem::path& path) -> Status {
  return Rendering::Detail::WritePngRgba8File(width, height, rgba, path);
}

auto RenderOutputExporter::WriteMultilayerExr(const RenderOutput& output, std::span<const RenderAov> selectedAovs,
                                              const std::filesystem::path& path,
                                              const RenderOutputBackground& background) -> Status {
  return Rendering::Detail::WriteMultilayerExrFile(output.Width, output.Height, output.GetPlanes(), selectedAovs, path,
                                                   background);
}

} // namespace Engine
