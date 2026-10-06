#include "EntityMembershipCommand.h"
#include "renderer/Entity.h"

EntityMembershipCommand::EntityMembershipCommand(
	std::shared_ptr<Engine::Scene> scene,
	std::shared_ptr<Renderer::Entity> entity,
	Engine::EntityId id,
	bool addOnExecute,
	ChangeCallback callback)
	: m_Scene(std::move(scene)), m_Entity(std::move(entity)), m_Id(id),
	  m_AddOnExecute(addOnExecute), m_OnChange(std::move(callback)) {}

void EntityMembershipCommand::SetPresent(bool present, bool undo) {
	if (!m_Scene || !m_Entity) return;
	if (present) {
		m_Id = m_Scene->AddEntity(m_Entity, m_Id);
		if (m_Id == Engine::InvalidEntityId) return;
	} else {
		m_Scene->RemoveEntity(m_Id);
	}
	if (m_OnChange) m_OnChange(m_Id, m_Entity, present, undo);
}

void EntityMembershipCommand::Execute() {
	SetPresent(m_AddOnExecute, false);
}

void EntityMembershipCommand::Undo() {
	SetPresent(!m_AddOnExecute, true);
}
