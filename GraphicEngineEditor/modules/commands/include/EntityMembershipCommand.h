#pragma once

#include "commands/include/Command.h"
#include "scene/include/Scene.h"
#include <functional>
#include <memory>

namespace Engine::Renderer
{
class Entity;
}

class EntityMembershipCommand final : public ICommand
{
  public:
    using ChangeCallback =
        std::function<void(Engine::EntityId, const std::shared_ptr<::Engine::Renderer::Entity> &, bool, bool)>;

    EntityMembershipCommand(std::shared_ptr<Engine::Scene> scene, std::shared_ptr<::Engine::Renderer::Entity> entity,
                            Engine::EntityId id, bool addOnExecute, ChangeCallback callback);
    void Execute() override;
    void Undo() override;

  private:
    void SetPresent(bool present, bool undo);
    std::shared_ptr<Engine::Scene> m_Scene;
    std::shared_ptr<::Engine::Renderer::Entity> m_Entity;
    Engine::EntityId m_Id;
    bool m_AddOnExecute;
    ChangeCallback m_OnChange;
};
