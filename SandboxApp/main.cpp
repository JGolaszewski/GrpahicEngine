#include <iostream>

#include "Application.h"
#include "backends/D3D11Backend.h"
#include "windows/GLFWWindow.h"

#include "Scene.h"
#include "renderer/Entity.h"
#include "renderer/Material.h"

class App : public Engine::Application {
	std::shared_ptr<Renderer::Camera> m_Camera;
protected:
	void OnInit() override {
		auto scene = std::make_shared<Engine::Scene>();
		m_Camera = std::make_shared<Renderer::Camera>(45.0f, 0.1f, 100.0f);
		m_Camera->m_Transform.SetPosition(0.0f, 0.0f, -5.0f);
		
		scene->AddCamera(m_Camera);

		auto material = std::make_shared<Renderer::Material>(0);
		GetRenderSystem()->GetMaterialSystem()->RegisterMaterial(material);

		ExecuteImmediate([this, scene, material](nvrhi::ICommandList* commandList) {
			Renderer::EntityBuilder builder(GetRenderer()->GetDevice(), commandList);
			auto cube = builder
				.Cube()
				.SetMaterial(material)
				.Scale(1.0f, 1.0f, 1.0f)
				.Position(0.0f, 0.0f, 0.0f)
				.Build();

			scene->AddEntity(cube);
		});

		AddScene(scene);

	}

	void OnUpdate(float deltaTime) override {
		std::cout << "Delta time: " << deltaTime << " seconds" << std::endl;
	}


};

int main() {
	
	App app;
	app.Run(std::make_unique<Engine::GLFWWindow>(1280, 720, "Sandbox"),
		std::make_unique<Engine::D3D11Backend>());

	return 0;
}
