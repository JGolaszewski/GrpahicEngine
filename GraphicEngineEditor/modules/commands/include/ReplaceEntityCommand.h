#pragma once

#include "commands/include/Command.h"
#include "scene/include/Scene.h"
#include "rendering/include/renderer/Entity.h"
#include <functional>
#include <memory>

class ReplaceEntityCommand final : public ICommand
{
  public:
    using Changed =
        std::function<void(Engine::EntityId, const std::shared_ptr<::Engine::Renderer::Entity> &)>;
    ReplaceEntityCommand(std::shared_ptr<Engine::Scene> scene, Engine::EntityId id,
                         std::shared_ptr<::Engine::Renderer::Entity> before,
                         std::shared_ptr<::Engine::Renderer::Entity> after, Changed changed);
    void Execute() override;
    void Undo() override;

  private:
    void Replace(const std::shared_ptr<::Engine::Renderer::Entity> &entity);
    std::shared_ptr<Engine::Scene> m_Scene;
    Engine::EntityId m_Id;
    std::shared_ptr<::Engine::Renderer::Entity> m_Before;
    std::shared_ptr<::Engine::Renderer::Entity> m_After;
    Changed m_Changed;
};
