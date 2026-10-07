#pragma once

#include "runtime/include/CoreExport.h"
#include "scene/include/SceneIds.h"

#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace Engine::Renderer
{
class SpotLight;
class Entity;
class Camera;
} // namespace Engine::Renderer

namespace Engine
{
class ENGINE_API Scene
{
  public:
    Scene() = default;
    float GetUnitsPerMeter() const noexcept
    {
        return m_UnitsPerMeter;
    }
    void SetUnitsPerMeter(float unitsPerMeter) noexcept
    {
        m_UnitsPerMeter = unitsPerMeter;
    }

    // Entities
    EntityId AddEntity(std::shared_ptr<::Engine::Renderer::Entity> entity, EntityId persistentId = InvalidEntityId);
    void RemoveEntity(::Engine::Renderer::Entity *entity);
    void RemoveEntity(EntityId id);
    bool ReplaceEntity(EntityId id, std::shared_ptr<::Engine::Renderer::Entity> entity);
    inline std::span<const std::shared_ptr<::Engine::Renderer::Entity>> GetEntities() const
    {
        return m_Entities;
    }
    inline size_t GetEntityCount() const
    {
        return m_Entities.size();
    }
    EntityId GetEntityIdAt(size_t index) const;
    EntityId GetEntityId(const ::Engine::Renderer::Entity *entity) const;
    std::shared_ptr<::Engine::Renderer::Entity> FindEntity(EntityId id) const;
    DirectX::XMFLOAT3 FindFreeSpawnPosition(const DirectX::XMFLOAT3 &initialPosition,
                                            float minimumDistance = 2.0f, float horizontalStep = 2.5f,
                                            uint32_t maxAttempts = 32) const;

    // Lights
    LightId AddLight(std::shared_ptr<::Engine::Renderer::SpotLight> light, LightId persistentId = InvalidLightId);
    void RemoveLight(::Engine::Renderer::SpotLight *light);
    void RemoveLight(LightId id);
    inline std::span<const std::shared_ptr<::Engine::Renderer::SpotLight>> GetLights() const
    {
        return m_Lights;
    }
    LightId GetLightIdAt(size_t index) const;
    LightId GetLightId(const ::Engine::Renderer::SpotLight *light) const;
    std::shared_ptr<::Engine::Renderer::SpotLight> FindLight(LightId id) const;

    // Cameras
    CameraId AddCamera(std::shared_ptr<::Engine::Renderer::Camera> camera, CameraId persistentId = InvalidCameraId);
    void RemoveCamera(::Engine::Renderer::Camera *camera);
    void RemoveCamera(CameraId id);
    inline std::span<const std::shared_ptr<::Engine::Renderer::Camera>> GetCameras() const
    {
        return m_Cameras;
    }
    CameraId GetCameraIdAt(size_t index) const;
    CameraId GetCameraId(const ::Engine::Renderer::Camera *camera) const;
    std::shared_ptr<::Engine::Renderer::Camera> FindCamera(CameraId id) const;

    void Update(float deltaTime);

    ::Engine::Renderer::Camera *GetCamera(uint32_t index) const;

  private:
    std::vector<std::shared_ptr<::Engine::Renderer::Entity>> m_Entities;
    std::vector<EntityId> m_EntityIds;
    std::unordered_map<EntityId, size_t> m_EntityIndices;
    std::vector<std::shared_ptr<::Engine::Renderer::SpotLight>> m_Lights;
    std::vector<LightId> m_LightIds;
    std::unordered_map<LightId, size_t> m_LightIndices;
    std::vector<std::shared_ptr<::Engine::Renderer::Camera>> m_Cameras;
    std::vector<CameraId> m_CameraIds;
    std::unordered_map<CameraId, size_t> m_CameraIndices;

    EntityId m_NextEntityId = 1;
    LightId m_NextLightId = 1;
    CameraId m_NextCameraId = 1;
    float m_UnitsPerMeter = 1.0f;
};
} // namespace Engine
