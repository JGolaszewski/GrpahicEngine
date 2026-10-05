#pragma once

#include "events/Event.h"

class AssetImportRequestedEvent final : public Engine::Events::Event {
public:
	static Engine::Events::EventType GetStaticType() { return Engine::Events::EventType::AssetImportRequested; }
	Engine::Events::EventType GetType() const override { return GetStaticType(); }
};
