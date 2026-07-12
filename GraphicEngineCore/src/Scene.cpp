#include "Scene.h"

#include "renderer/Entity.h"
#include "renderer/Light.h"

namespace Engine {

	void Scene::AddEntity(std::unique_ptr<Renderer::Entity> entity)
	{
		m_Entities.push_back(std::move(entity));
	}

	void Scene::RemoveEntity(Renderer::Entity* entity)
	{
		auto it = std::find_if(m_Entities.begin(), m_Entities.end(),
			[entity](const std::unique_ptr<Renderer::Entity>& ptr) {
				return ptr.get() == entity;
			});

		if (it != m_Entities.end()) {
			m_Entities.erase(it);
		}
	}

	void Scene::AddLight(std::unique_ptr<Renderer::Light> light)
	{
		m_Lights.push_back(std::move(light));

	}

	void Scene::RemoveLight(Renderer::Light* light)
	{
		auto it = std::find_if(m_Lights.begin(), m_Lights.end(),
			[light](const std::unique_ptr<Renderer::Light>& ptr) {
				return ptr.get() == light;
			});

		if (it != m_Lights.end()) {
			m_Lights.erase(it);
		}
	}

	void Scene::Update(float deltaTime)
	{
		// TODO
	}

	Renderer::Camera* Scene::GetCamera(uint32_t index) const
	{
		if (index >= m_Cameras.size()) {
			return nullptr;
		}

		return m_Cameras[index].get();
	}

}
