#pragma once

#include "AssetManager.h"
#include "EditorPanel.h"
#include <string>
#include <vector>

class AssetBrowserPanel final : public EditorPanel
{
  public:
    void Draw() override;
    void OnEvent(Engine::Events::Event &event) override;

  private:
    std::vector<Engine::AssetManager::ModelAssetInfo> m_Models;
    std::vector<Engine::AssetManager::TextureAssetInfo> m_Textures;
    std::string m_SelectedGuid;
};
