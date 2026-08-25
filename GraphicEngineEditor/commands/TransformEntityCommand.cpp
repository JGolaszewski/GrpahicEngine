#include "TransformEntityCommand.h"
#include "renderer/Entity.h"

// ================================================================================
// TransformEntityCommand.cpp
// ================================================================================
// ================================================================================

TransformEntityCommand::TransformEntityCommand(std::weak_ptr<Renderer::Entity> entity,
                                                 const DirectX::XMFLOAT3& oldPos,
                                                 const DirectX::XMFLOAT3& newPos)
    : m_Entity(entity)
    , m_OldPosition(oldPos)
    , m_NewPosition(newPos)
{
}

void TransformEntityCommand::Execute() {
	if (const auto entity = m_Entity.lock()) {
		entity->transform.SetPosition(m_NewPosition.x, m_NewPosition.y, m_NewPosition.z);
    }
}

void TransformEntityCommand::Undo() {
	if (const auto entity = m_Entity.lock()) {
		entity->transform.SetPosition(m_OldPosition.x, m_OldPosition.y, m_OldPosition.z);
    }
}
