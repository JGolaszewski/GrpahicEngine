#include "rendering/include/PrimitiveFactory.h"

#include "rendering/include/renderer/Entity.h"
#include "rendering/src/EntityBuilder.h"
#include "rendering/include/renderer/Material.h"

#include <nvrhi/nvrhi.h>

namespace Engine
{

struct PrimitiveFactory::Impl
{
    nvrhi::DeviceHandle Device;
};

PrimitiveFactory::PrimitiveFactory(void *device) : m_Impl(std::make_unique<Impl>())
{
    m_Impl->Device = static_cast<nvrhi::IDevice *>(device);
}

PrimitiveFactory::~PrimitiveFactory() = default;
PrimitiveFactory::PrimitiveFactory(PrimitiveFactory &&) noexcept = default;
PrimitiveFactory &PrimitiveFactory::operator=(PrimitiveFactory &&) noexcept = default;

std::shared_ptr<::Engine::Renderer::Entity> PrimitiveFactory::CreatePrimitive(
    PrimitiveShape shape, std::shared_ptr<::Engine::Renderer::Material> material,
    const DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT3 &scale)
{
    if (!m_Impl || !m_Impl->Device || !material)
    {
        return nullptr;
    }

    nvrhi::CommandListParameters parameters;
    parameters.setEnableImmediateExecution(true);
    nvrhi::CommandListHandle commandList = m_Impl->Device->createCommandList(parameters);
    if (!commandList)
    {
        return nullptr;
    }

    commandList->open();

    ::Engine::Renderer::EntityBuilder builder(m_Impl->Device, commandList);
    switch (shape)
    {
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

    auto entity = builder.Position(position.x, position.y, position.z)
                      .Scale(scale.x, scale.y, scale.z)
                      .SetMaterial(std::move(material))
                      .Build();

    commandList->close();
    if (entity)
    {
        m_Impl->Device->executeCommandList(commandList);
    }

    return entity;
}
} // namespace Engine
