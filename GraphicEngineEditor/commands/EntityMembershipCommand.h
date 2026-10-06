#pragma once

#include "Command.h"
#include "Scene.h"
#include <functional>
#include <memory>

namespace Renderer
{
class Entity;
}

class EntityMembershipCommand final : public ICommand
{
  public:
    using ChangeCallback =
        std::function<void(Engine::EntityId, const std::shared_ptr<Renderer::Entity> &, bool, bool)>;

    EntityMembershipCommand(std::shared_ptr<Engine::Scene> scene, std::shared_ptr<Renderer::Entity> entity,
                            Engine::EntityId id, bool addOnExecute, ChangeCallback callback);
    void Execute() override;
    void Undo() override;

  private:
    void SetPresent(bool present, bool undo);
    std::shared_ptr<Engine::Scene> m_Scene;
    std::shared_ptr<Renderer::Entity> m_Entity;
    Engine::EntityId m_Id;
    bool m_AddOnExecute;
    ChangeCallback m_OnChange;
};
