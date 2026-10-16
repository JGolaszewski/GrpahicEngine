#include "panels/include/AssetDropAssignment.h"
#include "panels/include/AssetDragDropPayload.h"

#include <algorithm>
#include <cstring>

namespace {
auto IsGuidCharacter(char character, size_t index) -> bool {
  if (index == 8 || index == 13 || index == 18 || index == 23) {
    return character == '-';
  }
  return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
         (character >= 'A' && character <= 'F');
}
} // namespace

auto MakeAssetDragDropPayload(std::string_view guid) noexcept -> std::optional<AssetDragDropPayload> {
  if (guid.size() != 36 || !std::ranges::all_of(guid, [index = std::size_t{0}](char character) mutable -> bool {
        return IsGuidCharacter(character, index++);
      })) {
    return std::nullopt;
  }
  AssetDragDropPayload payload;
  std::ranges::copy(guid, payload.Guid.begin());
  return payload;
}

auto ReadAssetDragDropGuid(const void* data, int size) -> std::optional<std::string> {
  if (data == nullptr || size != static_cast<int>(sizeof(AssetDragDropPayload))) {
    return std::nullopt;
  }
  AssetDragDropPayload payload;
  std::memcpy(&payload, data, sizeof(payload));
  if (payload.Guid.back() != '\0' || !std::ranges::all_of(payload.Guid.begin(), payload.Guid.end() - 1,
                                                          [index = std::size_t{0}](char character) mutable -> bool {
                                                            return IsGuidCharacter(character, index++);
                                                          })) {
    return std::nullopt;
  }
  return std::string(payload.Guid.data(), payload.Guid.size() - 1);
}

auto AssignDroppedModel(Engine::Ecs::MeshRenderer& mesh, std::string_view guid,
                        std::span<const Engine::AssetManager::ModelAssetInfo> models, std::string& error) -> bool {
  error.clear();
  if (guid.empty() || std::ranges::none_of(models, [guid](const auto& model) -> bool { return model.guid == guid; })) {
    error = "dropped model is no longer registered in this project";
    return false;
  }
  mesh.ModelGuid.assign(guid);
  mesh.PrimitiveType.clear();
  return true;
}

auto AssignDroppedTexture(Engine::SceneMaterialDocument& material, MaterialTextureSlot slot, std::string_view guid,
                          std::span<const Engine::AssetManager::TextureAssetInfo> textures, std::string& error)
    -> bool {
  error.clear();
  if (guid.empty() ||
      std::ranges::none_of(textures, [guid](const auto& texture) -> bool { return texture.guid == guid; })) {
    error = "dropped texture is no longer registered in this project";
    return false;
  }
  switch (slot) {
  case MaterialTextureSlot::BaseColor:
    material.baseColorTextureGuid = std::string(guid);
    return true;
  case MaterialTextureSlot::Normal:
    material.normalTextureGuid = std::string(guid);
    return true;
  }
  error = "material texture slot is unsupported";
  return false;
}
