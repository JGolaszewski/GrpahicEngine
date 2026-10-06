#pragma once

#include "Command.h"
#include "renderer/Material.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>

class ChangeMaterialTextureCommand final : public ICommand
{
  public:
    using Apply = std::function<bool(const std::shared_ptr<Renderer::Material> &,
                                     const std::shared_ptr<Renderer::TextureResource> &,
                                     const std::shared_ptr<Renderer::TextureResource> &,
                                     const std::optional<std::string> &, const std::optional<std::string> &)>;
    ChangeMaterialTextureCommand(
        std::shared_ptr<Renderer::Material> material, std::shared_ptr<Renderer::TextureResource> beforeBase,
        std::shared_ptr<Renderer::TextureResource> beforeNormal, std::optional<std::string> beforeBaseGuid,
        std::optional<std::string> beforeNormalGuid, std::shared_ptr<Renderer::TextureResource> afterBase,
        std::shared_ptr<Renderer::TextureResource> afterNormal, std::optional<std::string> afterBaseGuid,
        std::optional<std::string> afterNormalGuid, Apply apply);
    void Execute() override;
    void Undo() override;

  private:
    std::shared_ptr<Renderer::Material> m_Material;
    std::shared_ptr<Renderer::TextureResource> m_BeforeBase, m_BeforeNormal, m_AfterBase, m_AfterNormal;
    std::optional<std::string> m_BeforeBaseGuid, m_BeforeNormalGuid, m_AfterBaseGuid, m_AfterNormalGuid;
    Apply m_Apply;
};
