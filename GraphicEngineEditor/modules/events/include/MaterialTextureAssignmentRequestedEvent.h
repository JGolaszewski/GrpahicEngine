#pragma once

#include "events/include/AssetImportRequestedEvent.h"
#include "scene/include/Scene.h"
#include "runtime/include/events/Event.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

class MaterialTextureAssignmentRequestedEvent final : public Engine::Events::Event
{
  public:
    MaterialTextureAssignmentRequestedEvent(Engine::EntityId entityId, int32_t materialSlot,
                                            AssetImportRequestedEvent::TextureSemantic semantic,
                                            std::optional<std::string> textureGuid)
        : m_EntityId(entityId), m_MaterialSlot(materialSlot), m_Semantic(semantic),
          m_TextureGuid(std::move(textureGuid))
    {
    }
    static Engine::Events::EventType GetStaticType()
    {
        return Engine::Events::EventType::MaterialTextureAssignmentRequested;
    }
    Engine::Events::EventType GetType() const override
    {
        return GetStaticType();
    }
    Engine::EntityId GetEntityId() const noexcept
    {
        return m_EntityId;
    }
    int32_t GetMaterialSlot() const noexcept
    {
        return m_MaterialSlot;
    }
    AssetImportRequestedEvent::TextureSemantic GetSemantic() const noexcept
    {
        return m_Semantic;
    }
    const std::optional<std::string> &GetTextureGuid() const noexcept
    {
        return m_TextureGuid;
    }

  private:
    Engine::EntityId m_EntityId;
    int32_t m_MaterialSlot;
    AssetImportRequestedEvent::TextureSemantic m_Semantic;
    std::optional<std::string> m_TextureGuid;
};
