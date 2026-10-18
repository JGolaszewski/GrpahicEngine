#pragma once

#include "assets/include/AssetManager.h"
#include "runtime/include/events/Event.h"
#include <utility>
#include <vector>

class AssetRegistryChangedEvent final : public Engine::Events::Event {
public:
  explicit AssetRegistryChangedEvent(std::vector<Engine::AssetManager::ModelAssetInfo> models,
                                     std::vector<Engine::AssetManager::TextureAssetInfo> textures = {},
                                     std::vector<Engine::AssetManager::MaterialAssetInfo> materials = {})
      : m_Models(std::move(models)), m_Textures(std::move(textures)), m_Materials(std::move(materials)) {}
  static Engine::Events::EventType GetStaticType() { return Engine::Events::EventType::AssetRegistryChanged; }
  Engine::Events::EventType GetType() const override { return GetStaticType(); }
  const auto& GetModels() const noexcept { return m_Models; }
  const auto& GetTextures() const noexcept { return m_Textures; }
  const auto& GetMaterials() const noexcept { return m_Materials; }

private:
  std::vector<Engine::AssetManager::ModelAssetInfo> m_Models;
  std::vector<Engine::AssetManager::TextureAssetInfo> m_Textures;
  std::vector<Engine::AssetManager::MaterialAssetInfo> m_Materials;
};
