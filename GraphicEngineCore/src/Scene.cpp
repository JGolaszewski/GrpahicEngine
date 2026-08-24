#include "Scene.h"

#include "renderer/Entity.h"
#include "renderer/Light.h"

namespace Engine {

	void Scene::AddEntity(std::shared_ptr<Renderer::Entity> entity)
	{
		m_Entities.push_back(std::move(entity));
	}

	void Scene::RemoveEntity(Renderer::Entity* entity)
	{
		auto it = std::find_if(m_Entities.begin(), m_Entities.end(),
			[entity](const std::shared_ptr<Renderer::Entity>& ptr) {
				return ptr.get() == entity;
			});

		if (it != m_Entities.end()) {
			m_Entities.erase(it);
		}
	}

	void Scene::AddLight(std::shared_ptr<Renderer::SpotLight> light)
	{
		m_Lights.push_back(std::move(light));
	}

	void Scene::RemoveLight(Renderer::SpotLight* light)
	{
		auto it = std::find_if(m_Lights.begin(), m_Lights.end(),
			[light](const std::shared_ptr<Renderer::SpotLight>& ptr) {
				return ptr.get() == light;
			});

		if (it != m_Lights.end()) {
			m_Lights.erase(it);
		}
	}


	void Scene::AddCamera(std::shared_ptr<Renderer::Camera> camera)
	{
		m_Cameras.push_back(std::move(camera));
	}


	void Scene::RemoveCamera(Renderer::Camera* camera)
	{
		auto it = std::find_if(m_Cameras.begin(), m_Cameras.end(),
			[camera](const std::shared_ptr<Renderer::Camera>& ptr) {
				return ptr.get() == camera;
			});

		if (it != m_Cameras.end()) {
			m_Cameras.erase(it);
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
