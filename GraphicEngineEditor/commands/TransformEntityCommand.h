#pragma once

// ================================================================================
// TransformEntityCommand.h
// ================================================================================
// Concrete command that encapsulates a single entity translation.
// Stores the old and new positions locally so it can Undo/Redo without
// querying the ECS each time.
// ================================================================================

#include <DirectXMath.h>
#include "Command.h"

namespace Renderer {
    class Entity;
}

class TransformEntityCommand : public ICommand {
public:
    explicit TransformEntityCommand(Renderer::Entity* entity,
                                      const DirectX::XMFLOAT3& oldPos,
                                      const DirectX::XMFLOAT3& newPos);

    void Execute() override;
    void Undo() override;

    [[nodiscard]] Renderer::Entity* GetEntity() const noexcept { return m_Entity; }
    [[nodiscard]] const DirectX::XMFLOAT3& GetOldPosition() const noexcept { return m_OldPosition; }
    [[nodiscard]] const DirectX::XMFLOAT3& GetNewPosition() const noexcept { return m_NewPosition; }

private:
    Renderer::Entity* m_Entity;     // Non-owning; the entity outlives the command
    DirectX::XMFLOAT3 m_OldPosition;
    DirectX::XMFLOAT3 m_NewPosition;
};
