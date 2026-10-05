#pragma once

#include "Command.h"
#include "renderer/Material.h"
#include <memory>

class ChangeMaterialPropertiesCommand final : public ICommand {
public:
	ChangeMaterialPropertiesCommand(std::shared_ptr<Renderer::Material> material,
		Renderer::MaterialProperties before, Renderer::MaterialProperties after);
	void Execute() override;
	void Undo() override;

private:
	std::shared_ptr<Renderer::Material> m_Material;
	Renderer::MaterialProperties m_Before;
	Renderer::MaterialProperties m_After;
};
