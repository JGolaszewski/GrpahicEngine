#include <utility>

#include "commands/include/TransformEntityCommand.h"
#include "rendering/include/renderer/Entity.h"

// ================================================================================
// TransformEntityCommand.cpp
// ================================================================================
// ================================================================================

TransformEntityCommand::TransformEntityCommand(std::weak_ptr<::Engine::Renderer::Entity> entity,
                                               const DirectX::XMFLOAT3 &oldPos,
                                               const DirectX::XMFLOAT3 &newPos)
    : m_Entity(std::move(entity)), m_OldPosition(oldPos), m_NewPosition(newPos)
{
}

void TransformEntityCommand::Execute()
{
    if (const auto entity = m_Entity.lock())
    {
        entity->transform.SetPosition(m_NewPosition.x, m_NewPosition.y, m_NewPosition.z);
    }
}

void TransformEntityCommand::Undo()
{
    if (const auto entity = m_Entity.lock())
    {
        entity->transform.SetPosition(m_OldPosition.x, m_OldPosition.y, m_OldPosition.z);
    }
}
