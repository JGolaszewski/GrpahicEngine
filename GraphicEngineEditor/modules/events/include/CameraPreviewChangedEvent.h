#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/ArchetypeWorld.h"

/// Announces the currently selected camera's low-resolution viewport preview.
/// The texture handle is borrowed from the editor render target and remains valid
/// while that target is retained; a null handle and zero dimensions clear it.
class CameraPreviewChangedEvent final : public Engine::Events::Event {
public:
  CameraPreviewChangedEvent(Engine::EntityHandle entity, void* textureId, float width, float height)
      : m_Entity(entity), m_TextureId(textureId), m_Width(width), m_Height(height) {}

  static auto GetStaticType() -> Engine::Events::EventType { return Engine::Events::EventType::CameraPreviewChanged; }
  auto GetType() const -> Engine::Events::EventType override { return GetStaticType(); }
  [[nodiscard]] auto GetEntity() const noexcept -> Engine::EntityHandle { return m_Entity; }
  [[nodiscard]] auto GetTextureId() const noexcept -> void* { return m_TextureId; }
  [[nodiscard]] auto GetWidth() const noexcept -> float { return m_Width; }
  [[nodiscard]] auto GetHeight() const noexcept -> float { return m_Height; }

private:
  Engine::EntityHandle m_Entity = Engine::InvalidEntityHandle;
  void* m_TextureId = nullptr;
  float m_Width = 0.0F;
  float m_Height = 0.0F;
};
