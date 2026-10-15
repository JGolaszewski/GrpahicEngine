#include "rendering/include/PrimitiveFactory.h"

#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/RenderResources.h"
#include "rendering/src/PrimitiveMeshBuilder.h"

#include <nvrhi/nvrhi.h>

namespace Engine {

struct PrimitiveFactory::Impl {
  nvrhi::DeviceHandle Device;
};

PrimitiveFactory::PrimitiveFactory(void* device) : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Device = static_cast<nvrhi::IDevice*>(device);
}

PrimitiveFactory::~PrimitiveFactory() = default;
PrimitiveFactory::PrimitiveFactory(PrimitiveFactory&&) noexcept = default;
auto PrimitiveFactory::operator=(PrimitiveFactory&&) noexcept -> PrimitiveFactory& = default;

auto PrimitiveFactory::CreatePrimitive(PrimitiveShape shape, std::shared_ptr<::Engine::Renderer::Material> material)
    -> std::optional<::Engine::Renderer::Submesh> {
  if (!m_Impl || (m_Impl->Device == nullptr) || !material) {
    return std::nullopt;
  }

  nvrhi::CommandListParameters parameters;
  parameters.setEnableImmediateExecution(true);
  nvrhi::CommandListHandle commandList = m_Impl->Device->createCommandList(parameters);
  if (commandList == nullptr) {
    return std::nullopt;
  }

  commandList->open();

  ::Engine::Renderer::PrimitiveMeshBuilder builder(m_Impl->Device, commandList);
  switch (shape) {
  case PrimitiveShape::Cube:
    builder.Cube();
    break;
  case PrimitiveShape::Sphere:
    builder.Sphere();
    break;
  case PrimitiveShape::Plane:
    builder.Plane();
    break;
  case PrimitiveShape::Cone:
    builder.Cone(20);
    break;
  }

  auto submesh = builder.SetMaterial(std::move(material)).Build();

  commandList->close();
  if (submesh) {
    m_Impl->Device->executeCommandList(commandList);
  }

  return submesh;
}
} // namespace Engine
