#include "panels/include/AnalyticalFrameViewModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>

namespace AnalyticalFrame::AnalyticalFrameViewModel {
namespace {

struct NamedAov {
  Engine::RenderAov Aov;
  std::string_view Name;
};

constexpr std::array<NamedAov, 5> AovNames = {{{Engine::RenderAov::FinalColor, "finalColor"},
                                               {Engine::RenderAov::Depth, "depth"},
                                               {Engine::RenderAov::WorldNormal, "worldNormal"},
                                               {Engine::RenderAov::SpecularLighting, "specularLighting"},
                                               {Engine::RenderAov::DiffuseLighting, "diffuseLighting"}}};

struct NamedModel {
  Engine::Renderer::BrdfModel Model;
  std::string_view Name;
};

constexpr std::array<NamedModel, 9> ModelNames = {{{Engine::Renderer::BrdfModel::Legacy, "legacy"},
                                                   {Engine::Renderer::BrdfModel::GGX, "ggx"},
                                                   {Engine::Renderer::BrdfModel::GTR, "gtr"},
                                                   {Engine::Renderer::BrdfModel::Beckmann, "beckmann"},
                                                   {Engine::Renderer::BrdfModel::Gauss, "gauss"},
                                                   {Engine::Renderer::BrdfModel::BlinnPhong, "blinnPhong"},
                                                   {Engine::Renderer::BrdfModel::AshikhminShirley, "ashikhminShirley"},
                                                   {Engine::Renderer::BrdfModel::Schlick, "schlick"},
                                                   {Engine::Renderer::BrdfModel::Sawicki, "sawicki"}}};

auto AovName(Engine::RenderAov aov) -> std::string_view {
  const auto found =
      std::find_if(AovNames.begin(), AovNames.end(), [aov](const NamedAov& named) { return named.Aov == aov; });
  return found == AovNames.end() ? std::string_view{} : found->Name;
}

auto ParseAov(std::string_view name) -> std::optional<Engine::RenderAov> {
  const auto found =
      std::find_if(AovNames.begin(), AovNames.end(), [name](const NamedAov& named) { return named.Name == name; });
  return found == AovNames.end() ? std::nullopt : std::optional{found->Aov};
}

auto ModelName(Engine::Renderer::BrdfModel model) -> std::string_view {
  const auto found = std::find_if(ModelNames.begin(), ModelNames.end(),
                                  [model](const NamedModel& named) { return named.Model == model; });
  return found == ModelNames.end() ? std::string_view{} : found->Name;
}

auto ParseModel(std::string_view name) -> std::optional<Engine::Renderer::BrdfModel> {
  const auto found = std::find_if(ModelNames.begin(), ModelNames.end(),
                                  [name](const NamedModel& named) { return named.Name == name; });
  return found == ModelNames.end() ? std::nullopt : std::optional{found->Model};
}

auto ReadFiniteNumber(const nlohmann::json& value, std::string_view key) -> Engine::Result<float> {
  if (!value.contains(key) || !value.at(key).is_number()) {
    return std::unexpected(
        Engine::MakeError("Analytical Frame viewport state has an invalid numeric field: " + std::string(key)));
  }
  const float number = value.at(key).get<float>();
  if (!std::isfinite(number)) {
    return std::unexpected(Engine::MakeError("Analytical Frame viewport state contains a non-finite number."));
  }
  return number;
}

auto ReadModel(const nlohmann::json& value, std::string_view key) -> Engine::Result<Engine::Renderer::BrdfModel> {
  if (!value.contains(key) || !value.at(key).is_string()) {
    return std::unexpected(Engine::MakeError("Analytical Frame viewport state has an invalid model field."));
  }
  const auto model = ParseModel(value.at(key).get<std::string>());
  if (!model) {
    return std::unexpected(Engine::MakeError("Analytical Frame viewport state names an unsupported BRDF model."));
  }
  return *model;
}

auto ExpectedSampleCount(const Engine::RenderOutput& output, uint32_t channelCount) noexcept -> size_t {
  if (output.Width == 0 || output.Height == 0 || channelCount == 0) {
    return 0;
  }
  const size_t width = output.Width;
  const size_t height = output.Height;
  if (width > (std::numeric_limits<size_t>::max)() / height) {
    return 0;
  }
  const size_t pixels = width * height;
  if (pixels > (std::numeric_limits<size_t>::max)() / channelCount) {
    return 0;
  }
  return pixels * channelCount;
}

auto EncodeSrgb(float linear) noexcept -> uint8_t {
  const float safe = std::isfinite(linear) ? (std::max)(linear, 0.0F) : 0.0F;
  const float mapped = safe / (1.0F + safe);
  const float srgb = mapped <= 0.0031308F ? mapped * 12.92F : 1.055F * std::pow(mapped, 1.0F / 2.4F) - 0.055F;
  return static_cast<uint8_t>(std::lround(std::clamp(srgb, 0.0F, 1.0F) * 255.0F));
}

auto EncodeUnit(float value) noexcept -> uint8_t {
  const float safe = std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
  return static_cast<uint8_t>(std::lround(safe * 255.0F));
}

auto EncodePixel(const float* values, Engine::RenderAov aov) noexcept -> std::array<uint8_t, 4> {
  switch (aov) {
  case Engine::RenderAov::FinalColor:
    return {EncodeSrgb(values[0]), EncodeSrgb(values[1]), EncodeSrgb(values[2]), EncodeUnit(values[3])};
  case Engine::RenderAov::Depth: {
    const uint8_t depth = EncodeUnit(values[0]);
    return {depth, depth, depth, 255};
  }
  case Engine::RenderAov::WorldNormal:
    return {EncodeUnit(values[0] * 0.5F + 0.5F), EncodeUnit(values[1] * 0.5F + 0.5F),
            EncodeUnit(values[2] * 0.5F + 0.5F), 255};
  case Engine::RenderAov::SpecularLighting:
  case Engine::RenderAov::DiffuseLighting:
    return {EncodeSrgb(values[0]), EncodeSrgb(values[1]), EncodeSrgb(values[2]), 255};
  }
  return {0, 0, 0, 255};
}

auto GetValidAovPixels(const Engine::RenderOutput& output, Engine::RenderAov aov) noexcept -> std::span<const float> {
  const auto& descriptor = Engine::RenderOutput::GetDescriptor(aov);
  const auto pixels = output.GetPixels(aov);
  const size_t expected = ExpectedSampleCount(output, descriptor.ChannelCount);
  return expected != 0 && pixels.size() == expected ? pixels : std::span<const float>{};
}

auto IsFinite(std::span<const float> pixels) noexcept -> bool {
  return std::all_of(pixels.begin(), pixels.end(), [](float value) { return std::isfinite(value); });
}

} // namespace

auto ToViewportJson(const AnalyticalFrameViewState& state) -> nlohmann::json {
  nlohmann::json pinned = nullptr;
  if (state.PinnedPixel) {
    pinned = nlohmann::json::array({state.PinnedPixel->at(0), state.PinnedPixel->at(1)});
  }
  std::string_view mode = "Single";
  switch (state.Mode) {
  case AnalyticalFrameViewState::ComparisonMode::Single:
    mode = "Single";
    break;
  case AnalyticalFrameViewState::ComparisonMode::Wipe:
    mode = "Wipe";
    break;
  case AnalyticalFrameViewState::ComparisonMode::RelativeLuminanceError:
    mode = "RelativeLuminanceError";
    break;
  }
  return {{"SelectedAov", AovName(state.SelectedAov)},
          {"SelectedModel", ModelName(state.SelectedModel)},
          {"Zoom", state.Zoom},
          {"PanOffsetPixels", state.PanOffsetPixels},
          {"PinnedPixel", std::move(pinned)},
          {"CompareModelA", ModelName(state.CompareModelA)},
          {"CompareModelB", ModelName(state.CompareModelB)},
          {"Mode", mode},
          {"WipePosition", state.WipePosition}};
}

auto FromViewportJson(const nlohmann::json& value) -> Engine::Result<AnalyticalFrameViewState> {
  if (!value.is_object()) {
    return std::unexpected(Engine::MakeError("Analytical Frame viewport state must be an object."));
  }
  if (!value.contains("SelectedAov") || !value.at("SelectedAov").is_string()) {
    return std::unexpected(Engine::MakeError("Analytical Frame viewport state has an invalid AOV field."));
  }
  const auto aov = ParseAov(value.at("SelectedAov").get<std::string>());
  const auto selectedModel = ReadModel(value, "SelectedModel");
  const auto compareModelA = ReadModel(value, "CompareModelA");
  const auto compareModelB = ReadModel(value, "CompareModelB");
  const auto zoom = ReadFiniteNumber(value, "Zoom");
  const auto wipePosition = ReadFiniteNumber(value, "WipePosition");
  if (!aov || !selectedModel || !compareModelA || !compareModelB || !zoom || !wipePosition) {
    const std::string message = !aov             ? "Analytical Frame viewport state names an unsupported AOV."
                                : !selectedModel ? selectedModel.error().Message
                                : !compareModelA ? compareModelA.error().Message
                                : !compareModelB ? compareModelB.error().Message
                                : !zoom          ? zoom.error().Message
                                                 : wipePosition.error().Message;
    return std::unexpected(Engine::MakeError(message));
  }
  if (*zoom <= 0.0F || *wipePosition < 0.0F || *wipePosition > 1.0F) {
    return std::unexpected(Engine::MakeError("Analytical Frame viewport zoom or wipe position is out of range."));
  }
  if (!value.contains("PanOffsetPixels") || !value.at("PanOffsetPixels").is_array() ||
      value.at("PanOffsetPixels").size() != 2 || !value.at("PanOffsetPixels").at(0).is_number() ||
      !value.at("PanOffsetPixels").at(1).is_number()) {
    return std::unexpected(Engine::MakeError("Analytical Frame viewport pan offset must contain two numbers."));
  }
  const std::array<float, 2> pan{value.at("PanOffsetPixels").at(0).get<float>(),
                                 value.at("PanOffsetPixels").at(1).get<float>()};
  if (!std::isfinite(pan[0]) || !std::isfinite(pan[1])) {
    return std::unexpected(Engine::MakeError("Analytical Frame viewport pan offset must be finite."));
  }
  if (!value.contains("PinnedPixel")) {
    return std::unexpected(Engine::MakeError("Analytical Frame viewport state is missing its pinned-pixel field."));
  }
  std::optional<std::array<uint32_t, 2>> pinned;
  if (!value.at("PinnedPixel").is_null()) {
    const auto& pixel = value.at("PinnedPixel");
    if (!pixel.is_array() || pixel.size() != 2 || !pixel.at(0).is_number_unsigned() ||
        !pixel.at(1).is_number_unsigned() || pixel.at(0).get<uint64_t>() > UINT32_MAX ||
        pixel.at(1).get<uint64_t>() > UINT32_MAX) {
      return std::unexpected(
          Engine::MakeError("Analytical Frame pinned pixel must be null or two unsigned coordinates."));
    }
    pinned = std::array<uint32_t, 2>{pixel.at(0).get<uint32_t>(), pixel.at(1).get<uint32_t>()};
  }
  if (!value.contains("Mode") || !value.at("Mode").is_string()) {
    return std::unexpected(Engine::MakeError("Analytical Frame viewport state has an invalid comparison mode."));
  }
  const std::string mode = value.at("Mode").get<std::string>();
  const auto comparisonMode = mode == "Single" ? AnalyticalFrameViewState::ComparisonMode::Single
                              : mode == "Wipe" ? AnalyticalFrameViewState::ComparisonMode::Wipe
                              : mode == "RelativeLuminanceError"
                                  ? AnalyticalFrameViewState::ComparisonMode::RelativeLuminanceError
                                  : static_cast<AnalyticalFrameViewState::ComparisonMode>(UINT8_MAX);
  if (comparisonMode == static_cast<AnalyticalFrameViewState::ComparisonMode>(UINT8_MAX)) {
    return std::unexpected(Engine::MakeError("Analytical Frame viewport state names an unsupported comparison mode."));
  }
  if (comparisonMode == AnalyticalFrameViewState::ComparisonMode::RelativeLuminanceError &&
      *aov != Engine::RenderAov::FinalColor) {
    return std::unexpected(Engine::MakeError("Relative luminance error viewport state requires the final-color AOV."));
  }
  AnalyticalFrameViewState state;
  state.SelectedAov = *aov;
  state.SelectedModel = *selectedModel;
  state.Zoom = *zoom;
  state.PanOffsetPixels = pan;
  state.PinnedPixel = pinned;
  state.CompareModelA = *compareModelA;
  state.CompareModelB = *compareModelB;
  state.Mode = comparisonMode;
  state.WipePosition = *wipePosition;
  return state;
}

auto ReadPixel(const Engine::RenderOutput& output, Engine::RenderAov aov, uint32_t x, uint32_t y) noexcept
    -> std::optional<PixelSample> {
  if (x >= output.Width || y >= output.Height) {
    return std::nullopt;
  }
  const auto& descriptor = Engine::RenderOutput::GetDescriptor(aov);
  const auto pixels = GetValidAovPixels(output, aov);
  if (pixels.empty() || descriptor.ChannelCount > 4) {
    return std::nullopt;
  }
  const size_t offset = (static_cast<size_t>(y) * output.Width + x) * descriptor.ChannelCount;
  PixelSample result;
  result.ChannelCount = descriptor.ChannelCount;
  std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(offset), descriptor.ChannelCount, result.Channels.begin());
  return result;
}

auto BuildDisplayImage(const Engine::RenderOutput& output, Engine::RenderAov aov)
    -> Engine::Result<std::vector<uint8_t>> {
  const auto& descriptor = Engine::RenderOutput::GetDescriptor(aov);
  const auto pixels = GetValidAovPixels(output, aov);
  const size_t pixelCount = ExpectedSampleCount(output, 1);
  if (pixels.empty() || pixelCount == 0 || descriptor.ChannelCount > 4) {
    return std::unexpected(Engine::MakeError("The captured AOV plane is missing or malformed."));
  }
  std::vector<uint8_t> rgba(pixelCount * 4U);
  for (size_t index = 0; index < pixelCount; ++index) {
    const auto encoded = EncodePixel(pixels.data() + (index * descriptor.ChannelCount), aov);
    std::copy(encoded.begin(), encoded.end(), rgba.begin() + static_cast<std::ptrdiff_t>(index * 4U));
  }
  return rgba;
}

auto HasCompatibleLinearFinalColor(const Engine::RenderOutput& a, const Engine::RenderOutput& b) noexcept -> bool {
  if (a.Width == 0 || a.Height == 0 || a.Width != b.Width || a.Height != b.Height) {
    return false;
  }
  const auto aPixels = GetValidAovPixels(a, Engine::RenderAov::FinalColor);
  const auto bPixels = GetValidAovPixels(b, Engine::RenderAov::FinalColor);
  return !aPixels.empty() && aPixels.size() == bPixels.size() && IsFinite(aPixels) && IsFinite(bPixels);
}

auto ComputeRelativeLuminanceError(const Engine::RenderOutput& a, const Engine::RenderOutput& b)
    -> Engine::Result<std::vector<float>> {
  if (!HasCompatibleLinearFinalColor(a, b)) {
    return std::unexpected(
        Engine::MakeError("Relative luminance error requires compatible linear final-color buffers."));
  }
  constexpr float redWeight = 0.2126F;
  constexpr float greenWeight = 0.7152F;
  constexpr float blueWeight = 0.0722F;
  constexpr float minimumReference = 1.0e-6F;
  const auto aPixels = a.GetPixels(Engine::RenderAov::FinalColor);
  const auto bPixels = b.GetPixels(Engine::RenderAov::FinalColor);
  const size_t pixelCount = static_cast<size_t>(a.Width) * a.Height;
  std::vector<float> errors(pixelCount);
  for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
    const size_t offset = pixel * 4U;
    const float luminanceA =
        redWeight * aPixels[offset] + greenWeight * aPixels[offset + 1U] + blueWeight * aPixels[offset + 2U];
    const float luminanceB =
        redWeight * bPixels[offset] + greenWeight * bPixels[offset + 1U] + blueWeight * bPixels[offset + 2U];
    errors[pixel] = std::abs(luminanceB - luminanceA) / (std::max)(std::abs(luminanceA), minimumReference);
  }
  return errors;
}

auto BuildWipeImage(const Engine::RenderOutput& a, const Engine::RenderOutput& b, Engine::RenderAov aov,
                    float wipePosition) -> Engine::Result<std::vector<uint8_t>> {
  if (a.Width == 0 || a.Height == 0 || a.Width != b.Width || a.Height != b.Height || !std::isfinite(wipePosition)) {
    return std::unexpected(
        Engine::MakeError("A/B wipe requires same-sized captured AOV buffers and a finite position."));
  }
  auto imageA = BuildDisplayImage(a, aov);
  if (!imageA) {
    return std::unexpected(imageA.error());
  }
  auto imageB = BuildDisplayImage(b, aov);
  if (!imageB) {
    return std::unexpected(imageB.error());
  }
  const size_t width = a.Width;
  const size_t height = a.Height;
  const size_t split =
      static_cast<size_t>(std::lround(std::clamp(wipePosition, 0.0F, 1.0F) * static_cast<float>(a.Width)));
  std::vector<uint8_t> result(imageA->size());
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const auto& source = x < split ? *imageA : *imageB;
      const size_t offset = (y * width + x) * 4U;
      std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(offset), 4,
                  result.begin() + static_cast<std::ptrdiff_t>(offset));
    }
  }
  return result;
}

auto RelativeErrorHeatmap(float error) noexcept -> std::array<uint8_t, 4> {
  const float normalized = std::isfinite(error) ? std::clamp(error, 0.0F, 1.0F) : 1.0F;
  const float red = (std::min)(normalized * 2.0F, 1.0F);
  const float green = (std::max)(normalized * 2.0F - 1.0F, 0.0F);
  return {EncodeUnit(red), EncodeUnit(green), 0, 255};
}

auto BuildRelativeLuminanceHeatmap(const Engine::RenderOutput& a, const Engine::RenderOutput& b)
    -> Engine::Result<std::vector<uint8_t>> {
  auto errors = ComputeRelativeLuminanceError(a, b);
  if (!errors) {
    return std::unexpected(errors.error());
  }
  std::vector<uint8_t> rgba(errors->size() * 4U);
  for (size_t index = 0; index < errors->size(); ++index) {
    const auto color = RelativeErrorHeatmap(errors->at(index));
    std::copy(color.begin(), color.end(), rgba.begin() + static_cast<std::ptrdiff_t>(index * 4U));
  }
  return rgba;
}

} // namespace AnalyticalFrame::AnalyticalFrameViewModel
