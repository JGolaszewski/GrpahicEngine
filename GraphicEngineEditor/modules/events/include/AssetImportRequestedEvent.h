#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/Scene.h"
#include <cstdint>

class AssetImportRequestedEvent final : public Engine::Events::Event {
public:
  enum class AssetType { Model, Texture };
  enum class TextureSemantic { BaseColor, Normal };
  explicit AssetImportRequestedEvent(AssetType type = AssetType::Model,
                                     Engine::EntityHandle targetEntity = Engine::InvalidEntityHandle,
                                     int32_t materialSlot = -1, TextureSemantic semantic = TextureSemantic::BaseColor)
      : m_Type(type), m_TargetEntity(targetEntity), m_MaterialSlot(materialSlot), m_Semantic(semantic) {}
  static Engine::Events::EventType GetStaticType() { return Engine::Events::EventType::AssetImportRequested; }
  Engine::Events::EventType GetType() const override { return GetStaticType(); }
  AssetType GetAssetType() const noexcept { return m_Type; }
  Engine::EntityHandle GetTargetEntity() const noexcept { return m_TargetEntity; }
  int32_t GetMaterialSlot() const noexcept { return m_MaterialSlot; }
  TextureSemantic GetSemantic() const noexcept { return m_Semantic; }

private:
  AssetType m_Type;
  Engine::EntityHandle m_TargetEntity = Engine::InvalidEntityHandle;
  int32_t m_MaterialSlot = -1;
  TextureSemantic m_Semantic = TextureSemantic::BaseColor;
};
