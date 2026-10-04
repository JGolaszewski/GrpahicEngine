#pragma once

#include "CoreExport.h"

#include <DirectXMath.h>
#include <vector>
#include <memory>
#include <span>
#include <cstdint>

namespace Renderer {
	class SpotLight;
	class Entity;
	class Camera;
}

namespace Engine {
	using EntityId = uint64_t;
	using LightId = uint64_t;
	using CameraId = uint64_t;

	inline constexpr EntityId InvalidEntityId = 0;
	inline constexpr LightId InvalidLightId = 0;
	inline constexpr CameraId InvalidCameraId = 0;

	class ENGINE_API Scene {
	public:
		Scene() = default;
		
		// Entities
		EntityId AddEntity(std::shared_ptr<Renderer::Entity> entity);
		void RemoveEntity(Renderer::Entity* entity);
		void RemoveEntity(EntityId id);
		inline std::span<const std::shared_ptr<Renderer::Entity>> GetEntities() const {
			return m_Entities;
		}
		inline size_t GetEntityCount() const { return m_Entities.size(); }
		EntityId GetEntityIdAt(size_t index) const;
		EntityId GetEntityId(const Renderer::Entity* entity) const;
		std::shared_ptr<Renderer::Entity> FindEntity(EntityId id) const;
		DirectX::XMFLOAT3 FindFreeSpawnPosition(
			const DirectX::XMFLOAT3& initialPosition,
			float minimumDistance = 2.0f,
			float horizontalStep = 2.5f,
			uint32_t maxAttempts = 32) const;

		// Lights
		LightId AddLight(std::shared_ptr<Renderer::SpotLight> light);
		void RemoveLight(Renderer::SpotLight* light);
		void RemoveLight(LightId id);
		inline std::span<const std::shared_ptr<Renderer::SpotLight>> GetLights() const {
			return m_Lights;
		}
		LightId GetLightIdAt(size_t index) const;
		LightId GetLightId(const Renderer::SpotLight* light) const;
		std::shared_ptr<Renderer::SpotLight> FindLight(LightId id) const;

		// Cameras
		CameraId AddCamera(std::shared_ptr<Renderer::Camera> camera);
		void RemoveCamera(Renderer::Camera* camera);
		void RemoveCamera(CameraId id);
		inline std::span<const std::shared_ptr<Renderer::Camera>> GetCameras() const {
			return m_Cameras;
		}
		CameraId GetCameraIdAt(size_t index) const;
		CameraId GetCameraId(const Renderer::Camera* camera) const;
		std::shared_ptr<Renderer::Camera> FindCamera(CameraId id) const;

		void Update(float deltaTime);

		Renderer::Camera* GetCamera(uint32_t index) const;
	
	private:
		std::vector<std::shared_ptr<Renderer::Entity>> m_Entities;
		std::vector<EntityId> m_EntityIds;
		std::vector<std::shared_ptr<Renderer::SpotLight>> m_Lights;
		std::vector<LightId> m_LightIds;
		std::vector<std::shared_ptr<Renderer::Camera>> m_Cameras;
		std::vector<CameraId> m_CameraIds;

		EntityId m_NextEntityId = 1;
		LightId m_NextLightId = 1;
		CameraId m_NextCameraId = 1;
	};
}
