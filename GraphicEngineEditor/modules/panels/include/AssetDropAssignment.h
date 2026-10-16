#pragma once

#include "assets/include/AssetManager.h"
#include "scene/include/SceneDocument.h"
#include "scene/include/components/EcsComponents.h"

#include <span>
#include <string>
#include <string_view>

enum class MaterialTextureSlot { BaseColor, Normal };

/// Applies a validated model asset drop to a mesh renderer.
/// @param mesh Mesh component to update.
/// @param guid Dropped model identifier.
/// @param models Current registered model assets.
/// @param error Receives an incompatibility or stale-asset diagnostic.
/// @return True when the mesh renderer now references the selected model.
bool AssignDroppedModel(Engine::Ecs::MeshRenderer& mesh, std::string_view guid,
                        std::span<const Engine::AssetManager::ModelAssetInfo> models, std::string& error);

/// Applies a validated texture asset drop to a material texture slot.
/// @param material Material to update.
/// @param slot Target texture slot.
/// @param guid Dropped texture identifier.
/// @param textures Current registered texture assets.
/// @param error Receives an incompatibility or stale-asset diagnostic.
/// @return True when the material now references the selected texture.
bool AssignDroppedTexture(Engine::SceneMaterialDocument& material, MaterialTextureSlot slot, std::string_view guid,
                          std::span<const Engine::AssetManager::TextureAssetInfo> textures, std::string& error);
