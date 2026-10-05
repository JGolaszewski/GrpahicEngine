#pragma once

#include "EditorPanel.h"
#include "AssetManager.h"
#include <string>
#include <vector>

class AssetBrowserPanel final : public EditorPanel {
public:
	void Draw() override;
	void OnEvent(Engine::Events::Event& event) override;

private:
	std::vector<Engine::AssetManager::ModelAssetInfo> m_Models;
	std::string m_SelectedGuid;
};
