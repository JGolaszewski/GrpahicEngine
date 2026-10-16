#pragma once

#include "assets/include/AssetManager.h"
#include "panels/include/EditorPanel.h"
#include <array>
#include <string>
#include <vector>

class AssetBrowserPanel final : public EditorPanel {
public:
  void Draw() override;
  void OnEvent(Engine::Events::Event& event) override;

private:
  std::vector<Engine::AssetManager::ModelAssetInfo> m_Models;
  std::vector<Engine::AssetManager::TextureAssetInfo> m_Textures;
  std::string m_SelectedGuid;
  std::array<char, 256> m_RenameBuffer{};
  std::string m_StatusMessage;
  bool m_StatusSucceeded = true;
};
