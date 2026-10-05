#pragma once

#include "AssetManager.h"
#include "events/Event.h"
#include <utility>
#include <vector>

class AssetRegistryChangedEvent final : public Engine::Events::Event {
public:
	explicit AssetRegistryChangedEvent(std::vector<Engine::AssetManager::ModelAssetInfo> models)
		: m_Models(std::move(models)) {}
	static Engine::Events::EventType GetStaticType() { return Engine::Events::EventType::AssetRegistryChanged; }
	Engine::Events::EventType GetType() const override { return GetStaticType(); }
	const auto& GetModels() const noexcept { return m_Models; }

private:
	std::vector<Engine::AssetManager::ModelAssetInfo> m_Models;
};
