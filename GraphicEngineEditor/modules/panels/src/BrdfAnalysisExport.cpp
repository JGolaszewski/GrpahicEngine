#include "panels/include/BrdfAnalysisExport.h"

#include "rendering/include/RenderOutputExporter.h"
#include "scene/include/SceneDocument.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using Json = nlohmann::json;

constexpr uint32_t ChartWidth = 1200;
constexpr uint32_t ChartHeight = 720;
constexpr uint32_t PlotLeft = 68;
constexpr uint32_t PlotTop = 70;
constexpr uint32_t PlotRight = ChartWidth - 28;
constexpr uint32_t PlotBottom = ChartHeight - 58;

struct Color {
  uint8_t R;
  uint8_t G;
  uint8_t B;
};

auto ModelName(Engine::Renderer::BrdfModel model) -> std::string_view {
  using Engine::Renderer::BrdfModel;
  switch (model) {
  case BrdfModel::Legacy:
    return "LEGACY";
  case BrdfModel::GGX:
    return "GGX";
  case BrdfModel::GTR:
    return "GTR";
  case BrdfModel::Beckmann:
    return "BECKMANN";
  case BrdfModel::Gauss:
    return "GAUSS";
  case BrdfModel::BlinnPhong:
    return "BLINN-PHONG";
  case BrdfModel::AshikhminShirley:
    return "ASHIKHMIN-SHIRLEY";
  case BrdfModel::Schlick:
    return "SCHLICK";
  case BrdfModel::Sawicki:
    return "SAWICKI";
  }
  return "UNKNOWN";
}

auto ModelIdentifier(Engine::Renderer::BrdfModel model) -> std::string_view {
  using Engine::Renderer::BrdfModel;
  switch (model) {
  case BrdfModel::Legacy:
    return "legacy";
  case BrdfModel::GGX:
    return "ggx";
  case BrdfModel::GTR:
    return "gtr";
  case BrdfModel::Beckmann:
    return "beckmann";
  case BrdfModel::Gauss:
    return "gauss";
  case BrdfModel::BlinnPhong:
    return "blinnPhong";
  case BrdfModel::AshikhminShirley:
    return "ashikhminShirley";
  case BrdfModel::Schlick:
    return "schlick";
  case BrdfModel::Sawicki:
    return "sawicki";
  }
  return "unknown";
}

auto ModelColor(Engine::Renderer::BrdfModel model) -> Color {
  using Engine::Renderer::BrdfModel;
  switch (model) {
  case BrdfModel::Legacy:
    return {255, 196, 64};
  case BrdfModel::GGX:
    return {77, 180, 255};
  case BrdfModel::GTR:
    return {140, 225, 140};
  case BrdfModel::Beckmann:
    return {229, 117, 128};
  case BrdfModel::Gauss:
    return {192, 128, 255};
  case BrdfModel::BlinnPhong:
    return {255, 155, 64};
  case BrdfModel::AshikhminShirley:
    return {128, 220, 210};
  case BrdfModel::Schlick:
    return {255, 128, 200};
  case BrdfModel::Sawicki:
    return {180, 190, 100};
  }
  return {220, 220, 220};
}

auto Glyph(char character) -> std::array<uint8_t, 7> {
  switch (character) {
  case 'A':
    return {14, 17, 17, 31, 17, 17, 17};
  case 'B':
    return {30, 17, 17, 30, 17, 17, 30};
  case 'C':
    return {14, 17, 16, 16, 16, 17, 14};
  case 'D':
    return {30, 17, 17, 17, 17, 17, 30};
  case 'E':
    return {31, 16, 16, 30, 16, 16, 31};
  case 'F':
    return {31, 16, 16, 30, 16, 16, 16};
  case 'G':
    return {14, 17, 16, 23, 17, 17, 15};
  case 'H':
    return {17, 17, 17, 31, 17, 17, 17};
  case 'I':
    return {14, 4, 4, 4, 4, 4, 14};
  case 'K':
    return {17, 18, 20, 24, 20, 18, 17};
  case 'L':
    return {16, 16, 16, 16, 16, 16, 31};
  case 'M':
    return {17, 27, 21, 21, 17, 17, 17};
  case 'N':
    return {17, 25, 21, 19, 17, 17, 17};
  case 'O':
    return {14, 17, 17, 17, 17, 17, 14};
  case 'P':
    return {30, 17, 17, 30, 16, 16, 16};
  case 'R':
    return {30, 17, 17, 30, 20, 18, 17};
  case 'S':
    return {15, 16, 16, 14, 1, 1, 30};
  case 'T':
    return {31, 4, 4, 4, 4, 4, 4};
  case 'U':
    return {17, 17, 17, 17, 17, 17, 14};
  case 'W':
    return {17, 17, 17, 21, 21, 21, 10};
  case 'X':
    return {17, 17, 10, 4, 10, 17, 17};
  case 'Y':
    return {17, 17, 10, 4, 4, 4, 4};
  case '0':
    return {14, 17, 19, 21, 25, 17, 14};
  case '1':
    return {4, 12, 4, 4, 4, 4, 14};
  case '2':
    return {14, 17, 1, 2, 4, 8, 31};
  case '3':
    return {30, 1, 1, 14, 1, 1, 30};
  case '4':
    return {2, 6, 10, 18, 31, 2, 2};
  case '5':
    return {31, 16, 16, 30, 1, 1, 30};
  case '6':
    return {14, 16, 16, 30, 17, 17, 14};
  case '7':
    return {31, 1, 2, 4, 8, 8, 8};
  case '8':
    return {14, 17, 17, 14, 17, 17, 14};
  case '9':
    return {14, 17, 17, 15, 1, 1, 14};
  case '-':
    return {0, 0, 0, 31, 0, 0, 0};
  default:
    return {};
  }
}

class Raster final {
public:
  Raster() : m_Pixels(static_cast<size_t>(ChartWidth) * ChartHeight * 4U) {
    for (size_t offset = 0; offset < m_Pixels.size(); offset += 4U) {
      m_Pixels[offset] = 22;
      m_Pixels[offset + 1U] = 25;
      m_Pixels[offset + 2U] = 30;
      m_Pixels[offset + 3U] = 255;
    }
  }

  void Pixel(int x, int y, Color color) {
    if (x < 0 || y < 0 || x >= static_cast<int>(ChartWidth) || y >= static_cast<int>(ChartHeight)) {
      return;
    }
    const size_t offset = (static_cast<size_t>(y) * ChartWidth + static_cast<size_t>(x)) * 4U;
    m_Pixels[offset] = color.R;
    m_Pixels[offset + 1U] = color.G;
    m_Pixels[offset + 2U] = color.B;
    m_Pixels[offset + 3U] = 255;
  }

  void Line(int x0, int y0, int x1, int y1, Color color, int thickness = 1) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
      for (int yOffset = -thickness / 2; yOffset <= thickness / 2; ++yOffset) {
        for (int xOffset = -thickness / 2; xOffset <= thickness / 2; ++xOffset) {
          Pixel(x0 + xOffset, y0 + yOffset, color);
        }
      }
      if (x0 == x1 && y0 == y1) {
        break;
      }
      const int twiceError = 2 * error;
      if (twiceError >= dy) {
        error += dy;
        x0 += sx;
      }
      if (twiceError <= dx) {
        error += dx;
        y0 += sy;
      }
    }
  }

  void Text(int x, int y, std::string_view text, Color color) {
    for (const char character : text) {
      const auto glyph = Glyph(character);
      for (size_t row = 0; row < glyph.size(); ++row) {
        for (size_t column = 0; column < 5U; ++column) {
          if ((glyph.at(row) & (1U << (4U - column))) != 0U) {
            Pixel(x + static_cast<int>(column), y + static_cast<int>(row), color);
          }
        }
      }
      x += 7;
    }
  }

  [[nodiscard]] auto Pixels() const noexcept -> std::span<const uint8_t> { return m_Pixels; }

private:
  std::vector<uint8_t> m_Pixels;
};

auto WriteJsonFile(const std::filesystem::path& path, const Json& document) -> Engine::Status {
  if (path.empty() || path.extension() != ".json") {
    return std::unexpected(Engine::Error{"BRDF analysis JSON destination must have a .json extension"});
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return std::unexpected(Engine::Error{"Could not open BRDF analysis JSON destination: " + path.string()});
  }
  output << document.dump(2) << '\n';
  if (!output) {
    return std::unexpected(Engine::Error{"Could not write BRDF analysis JSON destination: " + path.string()});
  }
  return {};
}

auto BuildJson(const BrdfAnalysisPanel::CompletedComparison& comparison) -> Engine::Result<Json> {
  std::string sceneJson;
  std::string error;
  if (!Engine::SceneSerializer::SerializeJson(comparison.Snapshot.Scene, sceneJson, error)) {
    return std::unexpected(Engine::Error{"Could not serialize captured scene: " + error});
  }
  const auto scene = Json::parse(sceneJson);
  const auto& snapshot = comparison.Snapshot;
  const auto& sourceMaterial = snapshot.SourceMaterial;
  Json document = {
      {"formatVersion", 1},
      {"target", {{"label", comparison.TargetLabel}, {"entityId", snapshot.TargetEntityId}}},
      {"scene", scene},
      {"material",
       {{"source",
         {{"baseColor", sourceMaterial.baseColor},
          {"emissive", sourceMaterial.emissive},
          {"metallic", sourceMaterial.metallic},
          {"roughness", sourceMaterial.roughness},
          {"gtrGamma", sourceMaterial.gtrGamma},
          {"anisotropicRoughnessU", sourceMaterial.anisotropicRoughnessU},
          {"anisotropicRoughnessV", sourceMaterial.anisotropicRoughnessV},
          {"ior", sourceMaterial.ior},
          {"specularF0", sourceMaterial.specularF0},
          {"brdfModel", sourceMaterial.brdfModel},
          {"workflow", sourceMaterial.workflow},
          {"specularMode", sourceMaterial.specularMode},
          {"alphaMode", sourceMaterial.alphaMode},
          {"alphaCutoff", sourceMaterial.alphaCutoff},
          {"doubleSided", sourceMaterial.doubleSided},
          {"baseColorTextureGuid", sourceMaterial.baseColorTextureGuid},
          {"normalTextureGuid", sourceMaterial.normalTextureGuid}}},
        {"sourceProperties",
         {{"baseColor",
           {snapshot.SourceMaterialProperties.BaseColor.x, snapshot.SourceMaterialProperties.BaseColor.y,
            snapshot.SourceMaterialProperties.BaseColor.z, snapshot.SourceMaterialProperties.BaseColor.w}},
          {"emissive",
           {snapshot.SourceMaterialProperties.Emissive.x, snapshot.SourceMaterialProperties.Emissive.y,
            snapshot.SourceMaterialProperties.Emissive.z}},
          {"metallic", snapshot.SourceMaterialProperties.Metallic},
          {"roughness", snapshot.SourceMaterialProperties.Roughness},
          {"gtrGamma", snapshot.SourceMaterialProperties.GtrGamma},
          {"anisotropicRoughnessU", snapshot.SourceMaterialProperties.AnisotropicRoughnessU},
          {"anisotropicRoughnessV", snapshot.SourceMaterialProperties.AnisotropicRoughnessV},
          {"ior", snapshot.SourceMaterialProperties.Ior},
          {"specularF0",
           {snapshot.SourceMaterialProperties.SpecularF0.x, snapshot.SourceMaterialProperties.SpecularF0.y,
            snapshot.SourceMaterialProperties.SpecularF0.z}}}}}},
      {"camera",
       {{"position", snapshot.Camera.Position},
        {"rotationXyzw", snapshot.Camera.Rotation},
        {"fieldOfViewDegrees", snapshot.Camera.FieldOfViewDegrees},
        {"nearPlane", snapshot.Camera.NearPlane},
        {"farPlane", snapshot.Camera.FarPlane}}},
      {"render",
       {{"width", snapshot.Width},
        {"height", snapshot.Height},
        {"shadowMapResolution", snapshot.ShadowMapResolution},
        {"maxShadowCastersPerScene", snapshot.MaxShadowCastersPerScene},
        {"postProcessCallbackPresent", static_cast<bool>(snapshot.PostProcess)}}},
      {"variants", Json::array()},
  };
  if (snapshot.MaterialOverrideIndex) {
    document["target"]["materialOverrideIndex"] = *snapshot.MaterialOverrideIndex;
  }
  for (const auto& variant : comparison.Variants) {
    Json angles = variant.Curve.AnglesDegrees;
    Json response = variant.Curve.Response;
    document["variants"].push_back({
        {"model", ModelIdentifier(variant.Curve.Model)},
        {"curve",
         {{"anglesDegrees", std::move(angles)},
          {"response", std::move(response)},
          {"measurements",
           {{"peak", variant.Curve.Measurements.Peak},
            {"mean", variant.Curve.Measurements.Mean},
            {"integral", variant.Curve.Measurements.Integral}}}}},
        {"sceneLuminance", {{"mean", variant.SceneLuminance.Mean}, {"peak", variant.SceneLuminance.Peak}}},
        {"render",
         {{"width", variant.Render.Request.Width},
          {"height", variant.Render.Request.Height},
          {"shadowMapResolution", variant.Render.Request.ShadowMapResolution},
          {"maxShadowCastersPerScene", variant.Render.Request.MaxShadowCastersPerScene}}},
    });
  }
  return document;
}

auto BuildChart(const BrdfAnalysisPanel::CompletedComparison& comparison) -> Engine::Result<Raster> {
  if (comparison.Variants.empty()) {
    return std::unexpected(Engine::Error{"BRDF comparison has no displayed variants"});
  }
  float responseMaximum = 0.0F;
  for (const auto& variant : comparison.Variants) {
    if (variant.Curve.AnglesDegrees.size() != variant.Curve.Response.size() || variant.Curve.Response.size() < 2U) {
      return std::unexpected(Engine::Error{"BRDF comparison contains a malformed response curve"});
    }
    responseMaximum = (std::max)(responseMaximum, variant.Curve.Measurements.Peak);
  }
  responseMaximum = responseMaximum > 0.0F ? responseMaximum : 1.0F;

  Raster raster;
  const Color grid{62, 67, 74};
  const Color border{105, 112, 120};
  for (int tick = 0; tick <= 4; ++tick) {
    const int y = static_cast<int>(PlotBottom) - tick * static_cast<int>(PlotBottom - PlotTop) / 4;
    raster.Line(static_cast<int>(PlotLeft), y, static_cast<int>(PlotRight), y, tick == 0 ? border : grid);
  }
  raster.Line(static_cast<int>(PlotLeft), static_cast<int>(PlotTop), static_cast<int>(PlotLeft),
              static_cast<int>(PlotBottom), border);
  raster.Line(static_cast<int>(PlotRight), static_cast<int>(PlotTop), static_cast<int>(PlotRight),
              static_cast<int>(PlotBottom), border);

  for (const auto& variant : comparison.Variants) {
    const auto& curve = variant.Curve;
    int previousX = 0;
    int previousY = 0;
    bool hasPrevious = false;
    for (size_t index = 0; index < curve.Response.size(); ++index) {
      if (!std::isfinite(curve.AnglesDegrees.at(index)) || !std::isfinite(curve.Response.at(index))) {
        return std::unexpected(Engine::Error{"BRDF comparison contains a non-finite curve sample"});
      }
      const float xFraction = std::clamp(curve.AnglesDegrees.at(index) / 90.0F, 0.0F, 1.0F);
      const float yFraction = std::clamp(curve.Response.at(index) / responseMaximum, 0.0F, 1.0F);
      const int x = static_cast<int>(std::lround(PlotLeft + xFraction * (PlotRight - PlotLeft)));
      const int y = static_cast<int>(std::lround(PlotBottom - yFraction * (PlotBottom - PlotTop)));
      if (hasPrevious) {
        raster.Line(previousX, previousY, x, y, ModelColor(curve.Model), 2);
      }
      previousX = x;
      previousY = y;
      hasPrevious = true;
    }
  }
  for (size_t tick = 0; tick < 3U; ++tick) {
    constexpr std::array<int, 3> labels{0, 45, 90};
    const int x = static_cast<int>(PlotLeft) + static_cast<int>(tick) * static_cast<int>(PlotRight - PlotLeft) / 2;
    raster.Text(x - 6, static_cast<int>(PlotBottom) + 14, std::to_string(labels.at(tick)), {190, 195, 202});
  }

  int legendX = 70;
  int legendY = 24;
  for (const auto& variant : comparison.Variants) {
    const std::string_view name = ModelName(variant.Curve.Model);
    const int itemWidth = static_cast<int>(name.size()) * 7 + 34;
    if (legendX + itemWidth > static_cast<int>(ChartWidth) - 24 && legendX > 70) {
      legendX = 70;
      legendY += 20;
    }
    raster.Line(legendX, legendY + 3, legendX + 16, legendY + 3, ModelColor(variant.Curve.Model), 2);
    raster.Text(legendX + 22, legendY, name, {225, 228, 232});
    legendX += itemWidth;
  }
  return raster;
}

} // namespace

auto BrdfAnalysisExporter::WriteChartPng(const BrdfAnalysisPanel::CompletedComparison& comparison,
                                         const std::filesystem::path& path) -> Engine::Status {
  auto chart = BuildChart(comparison);
  if (!chart) {
    return std::unexpected(chart.error());
  }
  return Engine::RenderOutputExporter::WritePngRgba8(ChartWidth, ChartHeight, chart->Pixels(), path);
}

auto BrdfAnalysisExporter::WriteJson(const BrdfAnalysisPanel::CompletedComparison& comparison,
                                     const std::filesystem::path& path) -> Engine::Status {
  auto document = BuildJson(comparison);
  if (!document) {
    return std::unexpected(document.error());
  }
  return WriteJsonFile(path, *document);
}
