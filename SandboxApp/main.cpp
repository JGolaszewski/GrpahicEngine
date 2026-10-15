#include "runtime/include/Application.h"
#include "backends/D3D11Backend.h"
#include "windows/GLFWWindow.h"

#include "scene/include/Scene.h"
#include "rendering/include/renderer/Entity.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/RenderSystem.h"
#include "rendering/include/renderer/Camera.h"

class App : public Engine::Application
{
    std::shared_ptr<::Engine::Renderer::Camera> m_Camera;

  protected:
    void OnInit() override
    {
        auto scene = std::make_shared<Engine::Scene>();
        m_Camera = std::make_shared<::Engine::Renderer::Camera>(45.0f, 0.1f, 100.0f);
        m_Camera->transform.SetPosition(0.0f, 0.0f, -5.0f);

        scene->AddCamera(m_Camera);

        auto material = GetRenderSystem()->CreateMaterial(::Engine::Renderer::MaterialProperties{});

        auto cube = GetRenderSystem()->CreatePrimitive(Engine::PrimitiveShape::Cube, material,
                                                       DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        scene->AddEntity(std::move(cube));

        AddScene(scene);
    }

    void OnUpdate(float deltaTime) override
    {
        (void)deltaTime;
    }
};

int main()
{

    App app;
    app.Run(std::make_unique<Engine::GLFWWindow>(1280, 720, "Sandbox"),
            std::make_unique<Engine::D3D11Backend>());

    return 0;
}
