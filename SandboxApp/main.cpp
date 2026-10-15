#include "backends/D3D11Backend.h"
#include "runtime/include/Application.h"
#include "windows/GLFWWindow.h"

#include "rendering/include/RenderSystem.h"
#include "rendering/include/renderer/Material.h"
#include "scene/include/Scene.h"
#include "scene/include/components/EcsComponents.h"

#include <memory>
#include <utility>

class App : public Engine::Application {
protected:
  void OnInit() override {
    auto scene = std::make_shared<Engine::Scene>();
    const auto cameraSequence = scene->QueueCreate("Main Camera");
    const auto cubeSequence = scene->QueueCreate("Cube");
    const auto created = scene->sync();
    if (created.size() != 2 || created.at(0).Sequence != cameraSequence || created.at(1).Sequence != cubeSequence ||
        !created.at(0).Outcome || !created.at(1).Outcome) {
      return;
    }

    const Engine::EntityHandle camera = created.at(0).Entity;
    const Engine::EntityHandle cube = created.at(1).Entity;
    Engine::Ecs::LocalTransform cameraTransform;
    cameraTransform.Position = {0.0F, 0.0F, -5.0F};
    (void)scene->QueueAdd(camera, Engine::Ecs::Camera{});
    (void)scene->QueueSet(camera, cameraTransform);
    Engine::Ecs::MeshRenderer meshRenderer;
    meshRenderer.PrimitiveType = "cube";
    (void)scene->QueueAdd(cube, std::move(meshRenderer));
    (void)scene->sync();

    auto material = GetRenderSystem()->CreateMaterial(::Engine::Renderer::MaterialProperties{});
    (void)GetRenderSystem()->CreatePrimitive(Engine::PrimitiveShape::Cube, material);
    AddScene(scene);
  }

  void OnUpdate(float deltaTime) override { (void)deltaTime; }
};

auto main() -> int {
  App app;
  app.Run(std::make_unique<Engine::GLFWWindow>(1280, 720, "Sandbox"), std::make_unique<Engine::D3D11Backend>());
  return 0;
}
