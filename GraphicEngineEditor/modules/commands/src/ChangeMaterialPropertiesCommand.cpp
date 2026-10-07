#include "commands/include/ChangeMaterialPropertiesCommand.h"

ChangeMaterialPropertiesCommand::ChangeMaterialPropertiesCommand(std::shared_ptr<::Engine::Renderer::Material> material,
                                                                 ::Engine::Renderer::MaterialProperties before,
                                                                 ::Engine::Renderer::MaterialProperties after)
    : m_Material(std::move(material)), m_Before(before), m_After(after)
{
}

void ChangeMaterialPropertiesCommand::Execute()
{
    if (m_Material)
        m_Material->SetProperties(m_After);
}

void ChangeMaterialPropertiesCommand::Undo()
{
    if (m_Material)
        m_Material->SetProperties(m_Before);
}
