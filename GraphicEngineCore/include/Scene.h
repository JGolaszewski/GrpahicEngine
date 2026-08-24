#pragma once

#include "CoreExport.h"

#include <vector>
#include <memory>
#include <span>

namespace Renderer {
	class SpotLight;
	class Entity;
	class Camera;
}

namespace Engine {
	class ENGINE_API Scene {
	public:
		Scene() = default;
		
		// Entities
		void AddEntity(std::shared_ptr<Renderer::Entity> entity);
		void RemoveEntity(Renderer::Entity* entity);
		inline std::span<const std::shared_ptr<Renderer::Entity>> GetEntities() const {
			return m_Entities;
		}
		inline size_t GetEntityCount() const { return m_Entities.size(); }

		// Lights
		void AddLight(std::shared_ptr<Renderer::SpotLight> light);
		void RemoveLight(Renderer::SpotLight* light);
		inline std::span<const std::shared_ptr<Renderer::SpotLight>> GetLights() const {
			return m_Lights;
		}

		// Cameras
		void AddCamera(std::shared_ptr<Renderer::Camera> camera);
		void RemoveCamera(Renderer::Camera* camera);
		inline std::span<const std::shared_ptr<Renderer::Camera>> GetCameras() const {
			return m_Cameras;
		}

		void Update(float deltaTime);

		Renderer::Camera* GetCamera(uint32_t index) const;
	
	private:
		std::vector<std::shared_ptr<Renderer::Entity>> m_Entities;
		std::vector<std::shared_ptr<Renderer::SpotLight>> m_Lights;
		std::vector<std::shared_ptr<Renderer::Camera>> m_Cameras;
	};
}
