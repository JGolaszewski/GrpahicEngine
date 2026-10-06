#pragma once

#include "events/Event.h"

class AssetImportRequestedEvent final : public Engine::Events::Event {
public:
	enum class AssetType { Model, Texture };
	explicit AssetImportRequestedEvent(AssetType type = AssetType::Model) : m_Type(type) {}
	static Engine::Events::EventType GetStaticType() { return Engine::Events::EventType::AssetImportRequested; }
	Engine::Events::EventType GetType() const override { return GetStaticType(); }
	AssetType GetAssetType() const noexcept { return m_Type; }

private:
	AssetType m_Type;
};
