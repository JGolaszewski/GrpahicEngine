#pragma once

#include "assets/include/AssetImporter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Engine::Renderer::Detail {
struct TextureMipLevel {
  uint32_t Width = 0;
  uint32_t Height = 0;
  std::vector<uint8_t> Pixels;
};

inline auto DecodeMipColor(uint8_t value, bool srgb) -> float {
  const float normalized = static_cast<float>(value) / 255.0F;
  if (!srgb || normalized <= 0.04045F) {
    return srgb ? normalized / 12.92F : normalized;
  }
  return std::pow((normalized + 0.055F) / 1.055F, 2.4F);
}

inline auto EncodeMipColor(float value, bool srgb) -> uint8_t {
  value = std::clamp(value, 0.0F, 1.0F);
  if (srgb) {
    value = value <= 0.0031308F ? value * 12.92F : (1.055F * std::pow(value, 1.0F / 2.4F)) - 0.055F;
  }
  return static_cast<uint8_t>(std::lround(value * 255.0F));
}

inline auto BuildTextureMips(const Engine::ImportedImage& image, bool srgb, bool normalMap = false,
                             bool preserveAlphaCoverage = false) -> std::vector<TextureMipLevel> {
  std::vector<TextureMipLevel> mips;
  if (image.width == 0 || image.height == 0 || image.rgba8.size() != static_cast<size_t>(image.width) * image.height * 4) {
    return mips;
  }
  mips.push_back({.Width = image.width, .Height = image.height, .Pixels = image.rgba8});
  while (mips.back().Width > 1 || mips.back().Height > 1) {
    const TextureMipLevel& source = mips.back();
    TextureMipLevel mip;
    mip.Width = (std::max)(1U, source.Width / 2);
    mip.Height = (std::max)(1U, source.Height / 2);
    mip.Pixels.resize(static_cast<size_t>(mip.Width) * mip.Height * 4);
    uint32_t sourceCovered = 0;
    for (size_t pixel = 3; pixel < source.Pixels.size(); pixel += 4) {
      sourceCovered += source.Pixels[pixel] >= 128 ? 1U : 0U;
    }
    const float sourceCoverage = preserveAlphaCoverage
                                     ? static_cast<float>(sourceCovered) / static_cast<float>(source.Pixels.size() / 4)
                                     : 0.0F;
    for (uint32_t y = 0; y < mip.Height; ++y) {
      const uint32_t y0 = y * source.Height / mip.Height;
      const uint32_t y1 = (y + 1) * source.Height / mip.Height;
      for (uint32_t x = 0; x < mip.Width; ++x) {
        const uint32_t x0 = x * source.Width / mip.Width;
        const uint32_t x1 = (x + 1) * source.Width / mip.Width;
        std::array<float, 4> sum{};
        uint32_t sampleCount = 0;
        for (uint32_t sy = y0; sy < (std::max)(y0 + 1, y1); ++sy) {
          for (uint32_t sx = x0; sx < (std::max)(x0 + 1, x1); ++sx) {
            const size_t offset = (static_cast<size_t>(sy) * source.Width + sx) * 4;
            for (size_t channel = 0; channel < 3; ++channel) {
              sum[channel] += normalMap ? static_cast<float>(source.Pixels[offset + channel]) / 255.0F
                                        : DecodeMipColor(source.Pixels[offset + channel], srgb);
            }
            sum[3] += static_cast<float>(source.Pixels[offset + 3]) / 255.0F;
            ++sampleCount;
          }
        }
        for (float& channel : sum) {
          channel /= static_cast<float>(sampleCount);
        }
        if (normalMap) {
          float nx = sum[0] * 2.0F - 1.0F;
          float ny = sum[1] * 2.0F - 1.0F;
          float nz = sum[2] * 2.0F - 1.0F;
          const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
          if (length > 1.0e-6F) {
            nx /= length;
            ny /= length;
            nz /= length;
          } else {
            nx = 0.0F;
            ny = 0.0F;
            nz = 1.0F;
          }
          sum[0] = nx * 0.5F + 0.5F;
          sum[1] = ny * 0.5F + 0.5F;
          sum[2] = nz * 0.5F + 0.5F;
        }
        const size_t offset = (static_cast<size_t>(y) * mip.Width + x) * 4;
        for (size_t channel = 0; channel < 3; ++channel) {
          mip.Pixels[offset + channel] = EncodeMipColor(sum[channel], srgb && !normalMap);
        }
        mip.Pixels[offset + 3] = EncodeMipColor(sum[3], false);
      }
    }
    if (preserveAlphaCoverage && sourceCoverage > 0.0F && sourceCoverage < 1.0F) {
      const uint32_t targetCount = static_cast<uint32_t>(std::lround(sourceCoverage * mip.Width * mip.Height));
      std::vector<size_t> alphaOffsets;
      alphaOffsets.reserve(static_cast<size_t>(mip.Width) * mip.Height);
      for (size_t pixel = 3; pixel < mip.Pixels.size(); pixel += 4) {
        alphaOffsets.push_back(pixel);
      }
      std::ranges::stable_sort(alphaOffsets, [&mip](size_t left, size_t right) {
        return mip.Pixels[left] > mip.Pixels[right];
      });
      for (size_t rank = 0; rank < alphaOffsets.size(); ++rank) {
        uint8_t& alpha = mip.Pixels[alphaOffsets[rank]];
        if (rank < targetCount && alpha < 128) {
          alpha = 128;
        } else if (rank >= targetCount && alpha >= 128) {
          alpha = 127;
        }
      }
    }
    mips.push_back(std::move(mip));
  }
  return mips;
}
} // namespace Engine::Renderer::Detail
