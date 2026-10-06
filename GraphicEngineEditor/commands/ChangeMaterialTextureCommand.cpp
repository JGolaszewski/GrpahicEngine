#include "ChangeMaterialTextureCommand.h"

#include <utility>

ChangeMaterialTextureCommand::ChangeMaterialTextureCommand(
	std::shared_ptr<Renderer::Material> material,
	std::shared_ptr<Renderer::TextureResource> beforeBase, std::shared_ptr<Renderer::TextureResource> beforeNormal,
	std::optional<std::string> beforeBaseGuid, std::optional<std::string> beforeNormalGuid,
	std::shared_ptr<Renderer::TextureResource> afterBase, std::shared_ptr<Renderer::TextureResource> afterNormal,
	std::optional<std::string> afterBaseGuid, std::optional<std::string> afterNormalGuid, Apply apply)
	: m_Material(std::move(material)), m_BeforeBase(std::move(beforeBase)), m_BeforeNormal(std::move(beforeNormal)),
	  m_AfterBase(std::move(afterBase)), m_AfterNormal(std::move(afterNormal)),
	  m_BeforeBaseGuid(std::move(beforeBaseGuid)), m_BeforeNormalGuid(std::move(beforeNormalGuid)),
	  m_AfterBaseGuid(std::move(afterBaseGuid)), m_AfterNormalGuid(std::move(afterNormalGuid)),
	  m_Apply(std::move(apply)) {}

void ChangeMaterialTextureCommand::Execute() {
	if (m_Apply) m_Apply(m_Material, m_AfterBase, m_AfterNormal, m_AfterBaseGuid, m_AfterNormalGuid);
}

void ChangeMaterialTextureCommand::Undo() {
	if (m_Apply) m_Apply(m_Material, m_BeforeBase, m_BeforeNormal, m_BeforeBaseGuid, m_BeforeNormalGuid);
}
