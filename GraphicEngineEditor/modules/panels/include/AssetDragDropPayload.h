#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

inline constexpr char ModelAssetDragDropType[] = "GE_MODEL_ASSET";
inline constexpr char TextureAssetDragDropType[] = "GE_TEXTURE_ASSET";

struct AssetDragDropPayload {
  std::array<char, 37> Guid{};
};

/// Creates the fixed-size payload used by editor asset drag sources.
/// @param guid Persistent asset identifier.
/// @return A payload for a valid 36-character GUID, or empty for invalid input.
[[nodiscard]] auto MakeAssetDragDropPayload(std::string_view guid) noexcept -> std::optional<AssetDragDropPayload>;

/// Decodes and validates a drag payload before any asset lookup or component mutation.
/// @param data Opaque payload bytes received from ImGui.
/// @param size Payload byte count.
/// @return A GUID string when the payload has the expected size and terminator.
[[nodiscard]] auto ReadAssetDragDropGuid(const void* data, int size) -> std::optional<std::string>;
