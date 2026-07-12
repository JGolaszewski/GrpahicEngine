#pragma once

#include "CoreExport.h"

namespace Renderer {
	static class SpotLight {};
	static class Entity {};
}

namespace Engine {
	class ENGINE_API Scene {
	public:
		Scene() = default;
		
		// Entities
		void AddEntity(std::unique_ptr<Renderer::Entity> entity);
		void RemoveEntity(Renderer::Entity* entity);
		inline const std::vector<std::unique_ptr<Renderer::Entity>>& GetEntities() const {
			return m_Entities;
		}

		// Lights
		void AddLight(std::unique_ptr<Renderer::SpotLight> light);
		void RemoveLight(Renderer::SpotLight* light);
		inline const std::vector<std::unique_ptr<Renderer::SpotLight>>& GetLights() const {
			return m_Lights;
		}

		void Update(float deltaTime);

		Renderer::Camera* GetCamera(uint32_t index) const;

		inline size_t GetEntityCount() { return m_Entities.size(); }

		const std::vector<std::unique_ptr<Renderer::Entity>>& GetEntities() const { return m_Entities; }
	
	private:
		std::vector<std::unique_ptr<Renderer::Entity>> m_Entities;
		std::vector<std::unique_ptr<Renderer::SpotLight>> m_Lights;
		std::vector<std::unique_ptr<Renderer::Camera>> m_Cameras;
	};
}