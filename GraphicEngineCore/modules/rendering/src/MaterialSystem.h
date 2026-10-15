#pragma once

#include "rendering/include/renderer/Material.h"
#include "rendering/src/RenderDevice.h"
#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer {
class ENGINE_API MaterialSystem {
public:
  MaterialSystem(nvrhi::IDevice* device, nvrhi::ShaderHandle standardVS, nvrhi::ShaderHandle standardPS,
                 std::shared_ptr<TextureResource> fallbackColor, std::shared_ptr<TextureResource> fallbackNormal);
  ~MaterialSystem();

  bool RegisterMaterial(std::shared_ptr<Material> material);
  void UnregisterMaterial(std::shared_ptr<Material> material);
  std::shared_ptr<Material> CreateMaterial(const MaterialProperties& properties = {});
  bool SetTextureBindings(const std::shared_ptr<Material>& material, std::shared_ptr<TextureResource> baseColor,
                          std::shared_ptr<TextureResource> normal, std::optional<std::string> baseColorGuid = {},
                          std::optional<std::string> normalGuid = {}, bool setImportedDefaults = false);
  bool SetPbrTextureBindings(const std::shared_ptr<Material>& material, std::shared_ptr<TextureResource> baseColor,
                             std::shared_ptr<TextureResource> normal,
                             std::shared_ptr<TextureResource> metallicRoughness,
                             std::shared_ptr<TextureResource> occlusion, std::shared_ptr<TextureResource> emissive,
                             bool setImportedDefaults = false);
  void SetTexturedPixelShader(nvrhi::ShaderHandle shader) { m_TexturedPS = std::move(shader); }

  void UpdateDirtyMaterials(nvrhi::ICommandList* commandList);
  nvrhi::BindingLayoutHandle GetStandardMaterialLayout() const;

  inline nvrhi::ShaderHandle GetStandardVS() const { return m_StandardVS; }
  inline nvrhi::ShaderHandle GetStandardPS() const { return m_StandardPS; }
  inline nvrhi::ShaderHandle GetTexturedPS() const { return m_TexturedPS; }

private:
  nvrhi::DeviceHandle m_Device;
  nvrhi::BindingLayoutHandle m_StandardLayout;
  std::shared_ptr<TextureResource> m_FallbackColor;
  std::shared_ptr<TextureResource> m_FallbackNormal;

  nvrhi::ShaderHandle m_StandardVS;
  nvrhi::ShaderHandle m_StandardPS;
  nvrhi::ShaderHandle m_TexturedPS;

  std::vector<std::weak_ptr<Material>> m_ActiveMaterials;
  uint32_t m_NextMaterialID = 0;
};

} // namespace Engine::Renderer

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
