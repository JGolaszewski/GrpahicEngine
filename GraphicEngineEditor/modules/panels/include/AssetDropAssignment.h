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

/// Assigns a reusable material document and stable identity to a mesh renderer.
void AssignMaterialAsset(Engine::Ecs::MeshRenderer& mesh, std::string_view guid,
                         const Engine::SceneMaterialDocument& material);

/// Clears reusable and embedded material state so the renderer uses its built-in default.
void UseDefaultMaterial(Engine::Ecs::MeshRenderer& mesh);

/// Updates the embedded runtime copy when the mesh references the edited shared material.
/// @return True when the mesh references the GUID and was updated.
bool ApplyMaterialAssetEdit(Engine::Ecs::MeshRenderer& mesh, std::string_view guid,
                            const Engine::SceneMaterialDocument& material);
