#include "rendering/src/TextureMipGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Engine::Renderer::TextureMipGenerator {
namespace {
auto DecodeSrgb(float value) -> float {
  return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

auto EncodeSrgb(float value) -> float {
  value = std::clamp(value, 0.0F, 1.0F);
  return value <= 0.0031308F ? value * 12.92F : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

auto Read(const Mip& mip, uint32_t x, uint32_t y, bool srgb, bool normalMap) -> std::array<float, 4> {
  const size_t offset = (static_cast<size_t>(y) * mip.Width + x) * 4;
  std::array<float, 4> color{};
  for (size_t channel = 0; channel < 4; ++channel) {
    color[channel] = static_cast<float>(mip.Pixels[offset + channel]) / 255.0F;
  }
  if (normalMap) {
    for (size_t channel = 0; channel < 3; ++channel) {
      color[channel] = color[channel] * 2.0F - 1.0F;
    }
  } else if (srgb) {
    for (size_t channel = 0; channel < 3; ++channel) {
      color[channel] = DecodeSrgb(color[channel]);
    }
  }
  return color;
}

void Write(Mip& mip, uint32_t x, uint32_t y, std::array<float, 4> color, bool srgb, bool normalMap) {
  if (normalMap) {
    const float length = std::sqrt(color[0] * color[0] + color[1] * color[1] + color[2] * color[2]);
    if (length > 1.0e-6F) {
      for (size_t channel = 0; channel < 3; ++channel) {
        color[channel] /= length;
      }
    } else {
      color[0] = 0.0F;
      color[1] = 0.0F;
      color[2] = 1.0F;
    }
    for (size_t channel = 0; channel < 3; ++channel) {
      color[channel] = color[channel] * 0.5F + 0.5F;
    }
  } else if (srgb) {
    for (size_t channel = 0; channel < 3; ++channel) {
      color[channel] = EncodeSrgb(color[channel]);
    }
  }
  const size_t offset = (static_cast<size_t>(y) * mip.Width + x) * 4;
  for (size_t channel = 0; channel < 4; ++channel) {
    mip.Pixels[offset + channel] = static_cast<uint8_t>(std::lround(std::clamp(color[channel], 0.0F, 1.0F) * 255.0F));
  }
}

auto Coverage(const Mip& mip, float cutoff, float scale) -> float {
  const size_t count = static_cast<size_t>(mip.Width) * mip.Height;
  size_t covered = 0;
  for (size_t i = 0; i < count; ++i) {
    const float alpha = static_cast<float>(mip.Pixels[i * 4 + 3]) / 255.0F;
    covered += alpha * scale >= cutoff ? 1U : 0U;
  }
  return count == 0 ? 0.0F : static_cast<float>(covered) / static_cast<float>(count);
}

void PreserveCoverage(const Mip& base, Mip& mip, float cutoff) {
  if (cutoff <= 0.0F || cutoff > 1.0F) {
    return;
  }
  const float target = Coverage(base, cutoff, 1.0F);
  float bestScale = 1.0F;
  float bestError = std::abs(Coverage(mip, cutoff, bestScale) - target);
  float low = 0.0F;
  float high = 16.0F;
  for (int iteration = 0; iteration < 24; ++iteration) {
    const float scale = (low + high) * 0.5F;
    const float coverage = Coverage(mip, cutoff, scale);
    const float error = std::abs(coverage - target);
    if (error < bestError || (error == bestError && std::abs(scale - 1.0F) < std::abs(bestScale - 1.0F))) {
      bestScale = scale;
      bestError = error;
    }
    if (coverage < target) {
      low = scale;
    } else {
      high = scale;
    }
  }
  const size_t count = static_cast<size_t>(mip.Width) * mip.Height;
  for (size_t i = 0; i < count; ++i) {
    const float alpha = static_cast<float>(mip.Pixels[i * 4 + 3]) / 255.0F;
    mip.Pixels[i * 4 + 3] = static_cast<uint8_t>(std::lround(std::clamp(alpha * bestScale, 0.0F, 1.0F) * 255.0F));
  }
}
} // namespace

auto Build(const Engine::ImportedImage& image, Options options) -> std::vector<Mip> {
  if (image.width == 0 || image.height == 0 ||
      image.rgba8.size() != static_cast<size_t>(image.width) * image.height * 4) {
    return {};
  }
  std::vector<Mip> mips;
  mips.push_back({.Width = image.width, .Height = image.height, .Pixels = image.rgba8});
  while (mips.back().Width > 1 || mips.back().Height > 1) {
    const Mip& source = mips.back();
    Mip mip;
    mip.Width = (std::max)(1U, source.Width / 2);
    mip.Height = (std::max)(1U, source.Height / 2);
    mip.Pixels.resize(static_cast<size_t>(mip.Width) * mip.Height * 4);
    for (uint32_t y = 0; y < mip.Height; ++y) {
      for (uint32_t x = 0; x < mip.Width; ++x) {
        const double left = static_cast<double>(x) * source.Width / mip.Width;
        const double right = static_cast<double>(x + 1) * source.Width / mip.Width;
        const double top = static_cast<double>(y) * source.Height / mip.Height;
        const double bottom = static_cast<double>(y + 1) * source.Height / mip.Height;
        std::array<double, 4> sum{};
        double totalWeight = 0.0;
        for (uint32_t sourceY = static_cast<uint32_t>(std::floor(top));
             sourceY < static_cast<uint32_t>(std::ceil(bottom)); ++sourceY) {
          for (uint32_t sourceX = static_cast<uint32_t>(std::floor(left));
               sourceX < static_cast<uint32_t>(std::ceil(right)); ++sourceX) {
            const double weight =
                (std::min)(right, static_cast<double>(sourceX + 1)) - (std::max)(left, static_cast<double>(sourceX));
            const double vertical =
                (std::min)(bottom, static_cast<double>(sourceY + 1)) - (std::max)(top, static_cast<double>(sourceY));
            const double area = weight * vertical;
            if (area <= 0.0) {
              continue;
            }
            const auto value = Read(source, sourceX, sourceY, options.Srgb, options.NormalMap);
            for (size_t channel = 0; channel < 3; ++channel) {
              sum[channel] += static_cast<double>(value[channel]) * (options.NormalMap ? area : area * value[3]);
            }
            sum[3] += static_cast<double>(value[3]) * area;
            totalWeight += area;
          }
        }
        std::array<float, 4> filtered{};
        const double alpha = sum[3] / totalWeight;
        filtered[3] = static_cast<float>(alpha);
        for (size_t channel = 0; channel < 3; ++channel) {
          const double divisor = options.NormalMap ? totalWeight : sum[3];
          filtered[channel] = divisor > 1.0e-12 ? static_cast<float>(sum[channel] / divisor) : 0.0F;
        }
        Write(mip, x, y, filtered, options.Srgb, options.NormalMap);
      }
    }
    PreserveCoverage(mips.front(), mip, options.AlphaCutoff);
    mips.push_back(std::move(mip));
  }
  return mips;
}
} // namespace Engine::Renderer::TextureMipGenerator
