#include "rendering/include/RenderOutputExporter.h"

#include "RenderOutputExporterInternal.h"

#include <algorithm>

namespace Engine {

auto RenderOutputExporter::WritePng(const RenderOutput& output, const std::filesystem::path& path) -> Status {
  const auto finalColor = output.GetPixels(RenderAov::FinalColor);
  return Rendering::Detail::WritePngFile(output.Width, output.Height, finalColor, path);
}

auto RenderOutputExporter::WriteMultilayerExr(const RenderOutput& output, std::span<const RenderAov> selectedAovs,
                                              const std::filesystem::path& path) -> Status {
  return Rendering::Detail::WriteMultilayerExrFile(output.Width, output.Height, output.GetPlanes(), selectedAovs, path);
}

} // namespace Engine
