#include "Scene.h"

#include "renderer/Entity.h"
#include "renderer/Light.h"

#include <algorithm>

namespace Engine {

	EntityId Scene::AddEntity(std::shared_ptr<Renderer::Entity> entity)
	{
		if (!entity) return InvalidEntityId;
		const EntityId id = m_NextEntityId++;
		m_Entities.push_back(std::move(entity));
		m_EntityIds.push_back(id);
		return id;
	}

	void Scene::RemoveEntity(Renderer::Entity* entity)
	{
		auto it = std::find_if(m_Entities.begin(), m_Entities.end(),
			[entity](const std::shared_ptr<Renderer::Entity>& ptr) {
				return ptr.get() == entity;
			});

		if (it != m_Entities.end()) {
			const auto index = static_cast<size_t>(std::distance(m_Entities.begin(), it));
			m_Entities.erase(it);
			m_EntityIds.erase(m_EntityIds.begin() + index);
		}
	}

	void Scene::RemoveEntity(EntityId id)
	{
		auto it = std::find(m_EntityIds.begin(), m_EntityIds.end(), id);
		if (it != m_EntityIds.end()) {
			const auto index = static_cast<size_t>(std::distance(m_EntityIds.begin(), it));
			m_EntityIds.erase(it);
			m_Entities.erase(m_Entities.begin() + index);
		}
	}

	EntityId Scene::GetEntityIdAt(size_t index) const
	{
		return index < m_EntityIds.size() ? m_EntityIds[index] : InvalidEntityId;
	}

	EntityId Scene::GetEntityId(const Renderer::Entity* entity) const
	{
		for (size_t index = 0; index < m_Entities.size(); ++index) {
			if (m_Entities[index].get() == entity) return m_EntityIds[index];
		}
		return InvalidEntityId;
	}

	std::shared_ptr<Renderer::Entity> Scene::FindEntity(EntityId id) const
	{
		for (size_t index = 0; index < m_EntityIds.size(); ++index) {
			if (m_EntityIds[index] == id) return m_Entities[index];
		}
		return nullptr;
	}

	DirectX::XMFLOAT3 Scene::FindFreeSpawnPosition(
		const DirectX::XMFLOAT3& initialPosition,
		float minimumDistance,
		float horizontalStep,
		uint32_t maxAttempts) const
	{
		const float safeDistance = (std::max)(minimumDistance, 0.0f);
		const float safeStep = (std::max)(horizontalStep, 0.0f);
		const float minimumDistanceSquared = safeDistance * safeDistance;
		DirectX::XMFLOAT3 candidate = initialPosition;

		const auto intersectsExistingEntity = [this, minimumDistanceSquared](
			const DirectX::XMFLOAT3& position) {
			for (const auto& entity : m_Entities) {
				if (!entity) {
					continue;
				}

				const DirectX::XMFLOAT3& existingPosition = entity->transform.Position;
				const float dx = existingPosition.x - position.x;
				const float dy = existingPosition.y - position.y;
				const float dz = existingPosition.z - position.z;
				if ((dx * dx + dy * dy + dz * dz) < minimumDistanceSquared) {
					return true;
				}
			}

			return false;
		};

		for (uint32_t attempt = 0; attempt < maxAttempts && intersectsExistingEntity(candidate); ++attempt) {
			candidate.x += safeStep;
		}

		return candidate;
	}

	LightId Scene::AddLight(std::shared_ptr<Renderer::SpotLight> light)
	{
		if (!light) return InvalidLightId;
		const LightId id = m_NextLightId++;
		m_Lights.push_back(std::move(light));
		m_LightIds.push_back(id);
		return id;
	}

	void Scene::RemoveLight(Renderer::SpotLight* light)
	{
		auto it = std::find_if(m_Lights.begin(), m_Lights.end(),
			[light](const std::shared_ptr<Renderer::SpotLight>& ptr) {
				return ptr.get() == light;
			});

		if (it != m_Lights.end()) {
			const auto index = static_cast<size_t>(std::distance(m_Lights.begin(), it));
			m_Lights.erase(it);
			m_LightIds.erase(m_LightIds.begin() + index);
		}
	}

	void Scene::RemoveLight(LightId id)
	{
		auto it = std::find(m_LightIds.begin(), m_LightIds.end(), id);
		if (it != m_LightIds.end()) {
			const auto index = static_cast<size_t>(std::distance(m_LightIds.begin(), it));
			m_LightIds.erase(it);
			m_Lights.erase(m_Lights.begin() + index);
		}
	}

	LightId Scene::GetLightIdAt(size_t index) const
	{
		return index < m_LightIds.size() ? m_LightIds[index] : InvalidLightId;
	}

	LightId Scene::GetLightId(const Renderer::SpotLight* light) const
	{
		for (size_t index = 0; index < m_Lights.size(); ++index) {
			if (m_Lights[index].get() == light) return m_LightIds[index];
		}
		return InvalidLightId;
	}

	std::shared_ptr<Renderer::SpotLight> Scene::FindLight(LightId id) const
	{
		for (size_t index = 0; index < m_LightIds.size(); ++index) {
			if (m_LightIds[index] == id) return m_Lights[index];
		}
		return nullptr;
	}

	CameraId Scene::AddCamera(std::shared_ptr<Renderer::Camera> camera)
	{
		if (!camera) return InvalidCameraId;
		const CameraId id = m_NextCameraId++;
		m_Cameras.push_back(std::move(camera));
		m_CameraIds.push_back(id);
		return id;
	}

	void Scene::RemoveCamera(Renderer::Camera* camera)
	{
		auto it = std::find_if(m_Cameras.begin(), m_Cameras.end(),
			[camera](const std::shared_ptr<Renderer::Camera>& ptr) {
				return ptr.get() == camera;
			});

		if (it != m_Cameras.end()) {
			const auto index = static_cast<size_t>(std::distance(m_Cameras.begin(), it));
			m_Cameras.erase(it);
			m_CameraIds.erase(m_CameraIds.begin() + index);
		}
	}

	void Scene::RemoveCamera(CameraId id)
	{
		auto it = std::find(m_CameraIds.begin(), m_CameraIds.end(), id);
		if (it != m_CameraIds.end()) {
			const auto index = static_cast<size_t>(std::distance(m_CameraIds.begin(), it));
			m_CameraIds.erase(it);
			m_Cameras.erase(m_Cameras.begin() + index);
		}
	}

	CameraId Scene::GetCameraIdAt(size_t index) const
	{
		return index < m_CameraIds.size() ? m_CameraIds[index] : InvalidCameraId;
	}

	CameraId Scene::GetCameraId(const Renderer::Camera* camera) const
	{
		for (size_t index = 0; index < m_Cameras.size(); ++index) {
			if (m_Cameras[index].get() == camera) return m_CameraIds[index];
		}
		return InvalidCameraId;
	}

	std::shared_ptr<Renderer::Camera> Scene::FindCamera(CameraId id) const
	{
		for (size_t index = 0; index < m_CameraIds.size(); ++index) {
			if (m_CameraIds[index] == id) return m_Cameras[index];
		}
		return nullptr;
	}

	void Scene::Update(float deltaTime)
	{
		// Scene systems will be updated here once gameplay components are present.
		(void)deltaTime;
	}

	Renderer::Camera* Scene::GetCamera(uint32_t index) const
	{
		if (index >= m_Cameras.size()) {
			return nullptr;
		}

		return m_Cameras[index].get();
	}

}
