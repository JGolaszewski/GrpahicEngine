#include "commands/include/ReplaceEntityCommand.h"

#include <utility>

ReplaceEntityCommand::ReplaceEntityCommand(std::shared_ptr<Engine::Scene> scene, Engine::EntityId id,
                                           std::shared_ptr<::Engine::Renderer::Entity> before,
                                           std::shared_ptr<::Engine::Renderer::Entity> after, Changed changed)
    : m_Scene(std::move(scene)), m_Id(id), m_Before(std::move(before)), m_After(std::move(after)),
      m_Changed(std::move(changed))
{
}

void ReplaceEntityCommand::Replace(const std::shared_ptr<::Engine::Renderer::Entity> &entity)
{
    if (m_Scene && m_Scene->ReplaceEntity(m_Id, entity) && m_Changed)
        m_Changed(m_Id, entity);
}

void ReplaceEntityCommand::Execute()
{
    Replace(m_After);
}
void ReplaceEntityCommand::Undo()
{
    Replace(m_Before);
}
