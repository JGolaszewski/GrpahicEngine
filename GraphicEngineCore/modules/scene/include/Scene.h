#pragma once

#include "runtime/include/CoreExport.h"
#include "scene/include/Object.h"
#include "scene/include/SceneIds.h"

#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{
class SpotLight;
class Entity;
class Camera;
} // namespace Engine::Renderer

namespace Engine
{
/// Owns entities, lights, and cameras while maintaining stable scene-local IDs.
class ENGINE_API Scene
{
  public:
    /// Creates an empty scene with default units and no contained objects.
    Scene() = default;

    /// Returns the root object that owns all top-level scene nodes.
    /// @return Mutable reference to the scene root.
    Object &GetRoot() noexcept
    {
        return m_Root;
    }
    /// Returns the root object that owns all top-level scene nodes.
    /// @return Const reference to the scene root.
    const Object &GetRoot() const noexcept
    {
        return m_Root;
    }

    /// Returns the scene's unit-to-meter scale.
    /// @return Number of engine units per meter.
    float GetUnitsPerMeter() const noexcept
    {
        return m_UnitsPerMeter;
    }
    /// Sets the scale used to convert scene distances to meters.
    /// @param unitsPerMeter Number of engine units per meter.
    void SetUnitsPerMeter(float unitsPerMeter) noexcept
    {
        m_UnitsPerMeter = unitsPerMeter;
    }

    /// Adds an entity and assigns a unique persistent identifier.
    /// @param entity Entity to add.
    /// @param persistentId Requested serialized identifier, or InvalidEntityId for automatic assignment.
    /// @return Assigned identifier, or InvalidEntityId when the entity cannot be added.
    EntityId AddEntity(std::shared_ptr<::Engine::Renderer::Entity> entity,
                       EntityId persistentId = InvalidEntityId);

    /// Removes an entity by pointer and detaches its scene ownership.
    /// @param entity Entity to remove.
    void RemoveEntity(::Engine::Renderer::Entity *entity);

    /// Removes an entity by persistent identifier.
    /// @param id Identifier returned by AddEntity.
    void RemoveEntity(EntityId id);

    /// Replaces an existing entity while preserving its persistent identifier.
    /// @param id Identifier of the entity to replace.
    /// @param entity Replacement entity.
    /// @return True when the identifier exists and replacement succeeds.
    bool ReplaceEntity(EntityId id, std::shared_ptr<::Engine::Renderer::Entity> entity);

    /// Returns entities in stable scene order.
    /// @return Read-only span over the owned entity pointers.
    inline std::span<const std::shared_ptr<::Engine::Renderer::Entity>> GetEntities() const
    {
        return m_Entities;
    }
    /// Returns the number of entities in the scene.
    /// @return Entity count.
    inline size_t GetEntityCount() const
    {
        return m_Entities.size();
    }
    /// Returns the persistent identifier at an entity-list index.
    /// @param index Zero-based index in GetEntities.
    /// @return Entity identifier, or InvalidEntityId when index is out of range.
    EntityId GetEntityIdAt(size_t index) const;

    /// Looks up an entity's persistent identifier by pointer.
    /// @param entity Entity pointer to search for.
    /// @return Identifier, or InvalidEntityId when the pointer is not in this scene.
    EntityId GetEntityId(const ::Engine::Renderer::Entity *entity) const;

    /// Finds an entity by persistent identifier.
    /// @param id Identifier returned by AddEntity.
    /// @return Shared entity pointer, or empty when the identifier is unknown.
    std::shared_ptr<::Engine::Renderer::Entity> FindEntity(EntityId id) const;

    /// Searches for a spawn point separated from existing entity bounds.
    /// @param initialPosition Preferred world-space position.
    /// @param minimumDistance Required horizontal clearance from existing entities.
    /// @param horizontalStep Distance between candidate positions in the search grid.
    /// @param maxAttempts Maximum number of candidate positions to inspect.
    /// @return First acceptable position, or the initial position if no candidate is free.
    DirectX::XMFLOAT3 FindFreeSpawnPosition(const DirectX::XMFLOAT3 &initialPosition,
                                            float minimumDistance = 2.0f, float horizontalStep = 2.5f,
                                            uint32_t maxAttempts = 32) const;

    /// Adds a light and assigns a unique persistent identifier.
    /// @param light Light to add.
    /// @param persistentId Requested serialized identifier, or InvalidLightId for automatic assignment.
    /// @return Assigned identifier, or InvalidLightId when the light cannot be added.
    LightId AddLight(std::shared_ptr<::Engine::Renderer::SpotLight> light,
                     LightId persistentId = InvalidLightId);

    /// Removes a light by pointer.
    /// @param light Light to remove.
    void RemoveLight(::Engine::Renderer::SpotLight *light);

    /// Removes a light by persistent identifier.
    /// @param id Identifier returned by AddLight.
    void RemoveLight(LightId id);

    /// Returns lights in stable scene order.
    /// @return Read-only span over the owned light pointers.
    inline std::span<const std::shared_ptr<::Engine::Renderer::SpotLight>> GetLights() const
    {
        return m_Lights;
    }
    /// Returns the persistent light identifier at an index.
    /// @param index Zero-based index in GetLights.
    /// @return Light identifier, or InvalidLightId when index is out of range.
    LightId GetLightIdAt(size_t index) const;

    /// Looks up a light's persistent identifier by pointer.
    /// @param light Light pointer to search for.
    /// @return Identifier, or InvalidLightId when the pointer is not in this scene.
    LightId GetLightId(const ::Engine::Renderer::SpotLight *light) const;

    /// Finds a light by persistent identifier.
    /// @param id Identifier returned by AddLight.
    /// @return Shared light pointer, or empty when the identifier is unknown.
    std::shared_ptr<::Engine::Renderer::SpotLight> FindLight(LightId id) const;

    /// Adds a camera and assigns a unique persistent identifier.
    /// @param camera Camera to add.
    /// @param persistentId Requested serialized identifier, or InvalidCameraId for automatic assignment.
    /// @return Assigned identifier, or InvalidCameraId when the camera cannot be added.
    CameraId AddCamera(std::shared_ptr<::Engine::Renderer::Camera> camera,
                       CameraId persistentId = InvalidCameraId);

    /// Removes a camera by pointer.
    /// @param camera Camera to remove.
    void RemoveCamera(::Engine::Renderer::Camera *camera);

    /// Removes a camera by persistent identifier.
    /// @param id Identifier returned by AddCamera.
    void RemoveCamera(CameraId id);

    /// Returns cameras in stable scene order.
    /// @return Read-only span over the owned camera pointers.
    inline std::span<const std::shared_ptr<::Engine::Renderer::Camera>> GetCameras() const
    {
        return m_Cameras;
    }
    /// Returns the persistent camera identifier at an index.
    /// @param index Zero-based index in GetCameras.
    /// @return Camera identifier, or InvalidCameraId when index is out of range.
    CameraId GetCameraIdAt(size_t index) const;

    /// Looks up a camera's persistent identifier by pointer.
    /// @param camera Camera pointer to search for.
    /// @return Identifier, or InvalidCameraId when the pointer is not in this scene.
    CameraId GetCameraId(const ::Engine::Renderer::Camera *camera) const;

    /// Finds a camera by persistent identifier.
    /// @param id Identifier returned by AddCamera.
    /// @return Shared camera pointer, or empty when the identifier is unknown.
    std::shared_ptr<::Engine::Renderer::Camera> FindCamera(CameraId id) const;

    /// Updates all non-null entities in the scene.
    /// @param deltaTime Elapsed frame time in seconds.
    void Update(float deltaTime);

    /// Returns a borrowed camera pointer by list index.
    /// @param index Zero-based camera index.
    /// @return Camera pointer, or null when index is out of range.
    ::Engine::Renderer::Camera *GetCamera(uint32_t index) const;

  private:
    Object m_Root;
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

#ifdef _MSC_VER
#pragma warning(pop)
#endif
