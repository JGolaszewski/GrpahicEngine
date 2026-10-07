#pragma once

#include "commands/include/Command.h"
#include "rendering/include/renderer/Material.h"
#include <memory>

class ChangeMaterialPropertiesCommand final : public ICommand
{
  public:
    ChangeMaterialPropertiesCommand(std::shared_ptr<::Engine::Renderer::Material> material,
                                    ::Engine::Renderer::MaterialProperties before, ::Engine::Renderer::MaterialProperties after);
    void Execute() override;
    void Undo() override;

  private:
    std::shared_ptr<::Engine::Renderer::Material> m_Material;
    ::Engine::Renderer::MaterialProperties m_Before;
    ::Engine::Renderer::MaterialProperties m_After;
};
