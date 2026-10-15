#pragma once

#include "rendering/include/renderer/Material.h"

#include <nvrhi/nvrhi.h>

namespace Engine::Renderer {
class MaterialBackendAccess {
public:
  static nvrhi::BufferHandle GetConstantBuffer(const Material& material);
  static void SetConstantBuffer(Material& material, nvrhi::BufferHandle buffer);
  static nvrhi::BindingSetHandle GetBindingSet(const Material& material);
  static void SetBindingSet(Material& material, nvrhi::BindingSetHandle bindingSet);
};
} // namespace Engine::Renderer
