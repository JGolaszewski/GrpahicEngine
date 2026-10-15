#include "commands/include/EntityMembershipCommand.h"
#include "rendering/include/renderer/Entity.h"

EntityMembershipCommand::EntityMembershipCommand(std::shared_ptr<Engine::Scene> scene,
                                                 std::shared_ptr<::Engine::Renderer::Entity> entity,
                                                 Engine::EntityId id, bool addOnExecute,
                                                 ChangeCallback callback, bool alreadyPresent)
    : m_Scene(std::move(scene)), m_Entity(std::move(entity)), m_Id(id), m_AddOnExecute(addOnExecute),
      m_InitialPresenceAlreadyApplied(alreadyPresent), m_OnChange(std::move(callback))
{
}

void EntityMembershipCommand::SetPresent(bool present, bool undo)
{
    if (!m_Scene || !m_Entity)
        return;
    if (present)
    {
        m_Id = m_Scene->AddEntity(m_Entity, m_Id);
        if (m_Id == Engine::InvalidEntityId)
            return;
    }
    else
    {
        m_Scene->RemoveEntity(m_Id);
    }
    if (m_OnChange)
        m_OnChange(m_Id, m_Entity, present, undo);
}

void EntityMembershipCommand::Execute()
{
    if (m_InitialPresenceAlreadyApplied)
    {
        m_InitialPresenceAlreadyApplied = false;
        if (m_OnChange)
            m_OnChange(m_Id, m_Entity, true, false);
        return;
    }
    SetPresent(m_AddOnExecute, false);
}

void EntityMembershipCommand::Undo()
{
    SetPresent(!m_AddOnExecute, true);
}
