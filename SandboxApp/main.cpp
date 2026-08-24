#include <iostream>

#include "Application.h"

#include "Scene.h"

class App : public Engine::Application {
	std::shared_ptr<Renderer::Camera> m_Camera;
protected:
	void OnInit() override {
		auto scene = std::make_shared<Engine::Scene>();
		m_Camera = std::make_shared<Renderer::Camera>(45.0f, 0.1f, 100.0f);
		
		scene->AddCamera(m_Camera);

		
		Entity cube = EntityBuilder(GetRenderer()->GetDevice(), GetRenderSystem()->GetCommandList())
			.Cube()
			.SetMaterial(GetRenderSystem()->GetShaderFactory()->CreateMaterial("Default"))
			.Scale(1.0f, 1.0f, 1.0f)
			.Position(0.0f, 0.0f, 0.0f)
			.Build();


		scene->AddEntity(cube);

		AddScene(scene);

	}

	void OnUpdate(float deltaTime) override {
		std::cout << "Delta time: " << deltaTime << " seconds" << std::endl;
	}


};

int main() {
	
	App app;
	app.Run();

	return 0;
}