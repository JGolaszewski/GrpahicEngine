#include "scene/include/Scene.h"

#include "rendering/include/renderer/Entity.h"
#include "rendering/include/renderer/Light.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace Engine
{
namespace
{
template <typename Id>
void EraseIdAt(std::vector<Id> &ids, std::unordered_map<Id, size_t> &indices, size_t index)
{
    indices.erase(ids[index]);
    ids.erase(ids.begin() + index);
    for (size_t shifted = index; shifted < ids.size(); ++shifted)
    {
        indices[ids[shifted]] = shifted;
    }
}
} // namespace

EntityId Scene::AddEntity(std::shared_ptr<::Engine::Renderer::Entity> entity, EntityId persistentId)
{
    if (!entity)
        return InvalidEntityId;
    EntityId id = m_NextEntityId;
    if (persistentId != InvalidEntityId)
    {
        if (persistentId == std::numeric_limits<EntityId>::max() || m_EntityIndices.contains(persistentId))
            return InvalidEntityId;
        id = persistentId;
        m_NextEntityId = (std::max)(m_NextEntityId, persistentId + 1);
    }
    else
    {
        if (m_NextEntityId == InvalidEntityId)
            return InvalidEntityId;
        ++m_NextEntityId;
    }
    m_Entities.push_back(std::move(entity));
    m_EntityIds.push_back(id);
    m_EntityIndices.emplace(id, m_EntityIds.size() - 1);
    return id;
}

void Scene::RemoveEntity(::Engine::Renderer::Entity *entity)
{
    auto it = std::find_if(
        m_Entities.begin(), m_Entities.end(),
        [entity](const std::shared_ptr<::Engine::Renderer::Entity> &ptr) { return ptr.get() == entity; });

    if (it != m_Entities.end())
    {
        const auto index = static_cast<size_t>(std::distance(m_Entities.begin(), it));
        m_Entities.erase(it);
        EraseIdAt(m_EntityIds, m_EntityIndices, index);
    }
}

void Scene::RemoveEntity(EntityId id)
{
    auto it = m_EntityIndices.find(id);
    if (it != m_EntityIndices.end())
    {
        const size_t index = it->second;
        EraseIdAt(m_EntityIds, m_EntityIndices, index);
        m_Entities.erase(std::next(m_Entities.begin(), static_cast<std::ptrdiff_t>(index)));
    }
}

bool Scene::ReplaceEntity(EntityId id, std::shared_ptr<::Engine::Renderer::Entity> entity)
{
    if (!entity)
        return false;
    const auto it = m_EntityIndices.find(id);
    if (it == m_EntityIndices.end())
        return false;
    m_Entities[it->second] = std::move(entity);
    return true;
}

EntityId Scene::GetEntityIdAt(size_t index) const
{
    return index < m_EntityIds.size() ? m_EntityIds[index] : InvalidEntityId;
}

EntityId Scene::GetEntityId(const ::Engine::Renderer::Entity *entity) const
{
    for (size_t index = 0; index < m_Entities.size(); ++index)
    {
        if (m_Entities[index].get() == entity)
            return m_EntityIds[index];
    }
    return InvalidEntityId;
}

std::shared_ptr<::Engine::Renderer::Entity> Scene::FindEntity(EntityId id) const
{
    const auto it = m_EntityIndices.find(id);
    return it == m_EntityIndices.end() ? nullptr : m_Entities[it->second];
}

DirectX::XMFLOAT3 Scene::FindFreeSpawnPosition(const DirectX::XMFLOAT3 &initialPosition,
                                               float minimumDistance, float horizontalStep,
                                               uint32_t maxAttempts) const
{
    const float safeDistance = (std::max)(minimumDistance, 0.0f);
    const float safeStep = (std::max)(horizontalStep, 0.0f);
    const float minimumDistanceSquared = safeDistance * safeDistance;
    DirectX::XMFLOAT3 candidate = initialPosition;

    const auto intersectsExistingEntity = [this, minimumDistanceSquared](const DirectX::XMFLOAT3 &position) {
        for (const auto &entity : m_Entities)
        {
            if (!entity)
            {
                continue;
            }

            const DirectX::XMFLOAT3 &existingPosition = entity->transform.Position;
            const float dx = existingPosition.x - position.x;
            const float dy = existingPosition.y - position.y;
            const float dz = existingPosition.z - position.z;
            if ((dx * dx + dy * dy + dz * dz) < minimumDistanceSquared)
            {
                return true;
            }
        }

        return false;
    };

    for (uint32_t attempt = 0; attempt < maxAttempts && intersectsExistingEntity(candidate); ++attempt)
    {
        candidate.x += safeStep;
    }

    return candidate;
}

LightId Scene::AddLight(std::shared_ptr<::Engine::Renderer::SpotLight> light, LightId persistentId)
{
    if (!light)
        return InvalidLightId;
    LightId id = m_NextLightId;
    if (persistentId != InvalidLightId)
    {
        if (persistentId == std::numeric_limits<LightId>::max() || m_LightIndices.contains(persistentId))
            return InvalidLightId;
        id = persistentId;
        m_NextLightId = (std::max)(m_NextLightId, persistentId + 1);
    }
    else
    {
        if (m_NextLightId == InvalidLightId)
            return InvalidLightId;
        ++m_NextLightId;
    }
    m_Lights.push_back(std::move(light));
    m_LightIds.push_back(id);
    m_LightIndices.emplace(id, m_LightIds.size() - 1);
    return id;
}

void Scene::RemoveLight(::Engine::Renderer::SpotLight *light)
{
    auto it = std::find_if(
        m_Lights.begin(), m_Lights.end(),
        [light](const std::shared_ptr<::Engine::Renderer::SpotLight> &ptr) { return ptr.get() == light; });

    if (it != m_Lights.end())
    {
        const auto index = static_cast<size_t>(std::distance(m_Lights.begin(), it));
        m_Lights.erase(it);
        EraseIdAt(m_LightIds, m_LightIndices, index);
    }
}

void Scene::RemoveLight(LightId id)
{
    auto it = m_LightIndices.find(id);
    if (it != m_LightIndices.end())
    {
        const size_t index = it->second;
        EraseIdAt(m_LightIds, m_LightIndices, index);
        m_Lights.erase(std::next(m_Lights.begin(), static_cast<std::ptrdiff_t>(index)));
    }
}

LightId Scene::GetLightIdAt(size_t index) const
{
    return index < m_LightIds.size() ? m_LightIds[index] : InvalidLightId;
}

LightId Scene::GetLightId(const ::Engine::Renderer::SpotLight *light) const
{
    for (size_t index = 0; index < m_Lights.size(); ++index)
    {
        if (m_Lights[index].get() == light)
            return m_LightIds[index];
    }
    return InvalidLightId;
}

std::shared_ptr<::Engine::Renderer::SpotLight> Scene::FindLight(LightId id) const
{
    const auto it = m_LightIndices.find(id);
    return it == m_LightIndices.end() ? nullptr : m_Lights[it->second];
}

CameraId Scene::AddCamera(std::shared_ptr<::Engine::Renderer::Camera> camera, CameraId persistentId)
{
    if (!camera)
        return InvalidCameraId;
    CameraId id = m_NextCameraId;
    if (persistentId != InvalidCameraId)
    {
        if (persistentId == std::numeric_limits<CameraId>::max() || m_CameraIndices.contains(persistentId))
            return InvalidCameraId;
        id = persistentId;
        m_NextCameraId = (std::max)(m_NextCameraId, persistentId + 1);
    }
    else
    {
        if (m_NextCameraId == InvalidCameraId)
            return InvalidCameraId;
        ++m_NextCameraId;
    }
    m_Cameras.push_back(std::move(camera));
    m_CameraIds.push_back(id);
    m_CameraIndices.emplace(id, m_CameraIds.size() - 1);
    return id;
}

void Scene::RemoveCamera(::Engine::Renderer::Camera *camera)
{
    auto it = std::find_if(
        m_Cameras.begin(), m_Cameras.end(),
        [camera](const std::shared_ptr<::Engine::Renderer::Camera> &ptr) { return ptr.get() == camera; });

    if (it != m_Cameras.end())
    {
        const auto index = static_cast<size_t>(std::distance(m_Cameras.begin(), it));
        m_Cameras.erase(it);
        EraseIdAt(m_CameraIds, m_CameraIndices, index);
    }
}

void Scene::RemoveCamera(CameraId id)
{
    auto it = m_CameraIndices.find(id);
    if (it != m_CameraIndices.end())
    {
        const size_t index = it->second;
        EraseIdAt(m_CameraIds, m_CameraIndices, index);
        m_Cameras.erase(std::next(m_Cameras.begin(), static_cast<std::ptrdiff_t>(index)));
    }
}

CameraId Scene::GetCameraIdAt(size_t index) const
{
    return index < m_CameraIds.size() ? m_CameraIds[index] : InvalidCameraId;
}

CameraId Scene::GetCameraId(const ::Engine::Renderer::Camera *camera) const
{
    for (size_t index = 0; index < m_Cameras.size(); ++index)
    {
        if (m_Cameras[index].get() == camera)
            return m_CameraIds[index];
    }
    return InvalidCameraId;
}

std::shared_ptr<::Engine::Renderer::Camera> Scene::FindCamera(CameraId id) const
{
    const auto it = m_CameraIndices.find(id);
    return it == m_CameraIndices.end() ? nullptr : m_Cameras[it->second];
}

void Scene::Update(float deltaTime)
{
    // Scene systems will be updated here once gameplay components are present.
    (void)deltaTime;
}

::Engine::Renderer::Camera *Scene::GetCamera(uint32_t index) const
{
    if (index >= m_Cameras.size())
    {
        return nullptr;
    }

    return m_Cameras[index].get();
}

} // namespace Engine
