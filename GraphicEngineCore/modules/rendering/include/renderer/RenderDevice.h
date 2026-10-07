#pragma once

#include "assets/include/AssetImporter.h"
#include "runtime/include/CoreExport.h"
#include "rendering/include/renderer/Mesh.h"

#include <memory>
#include <nvrhi/nvrhi.h>
#include <string>
#include <vector>

namespace Engine::Renderer
{
struct TextureResource
{
    nvrhi::TextureHandle Texture;
    nvrhi::SamplerHandle Sampler;
};

class ENGINE_API RenderDevice
{
  public:
    explicit RenderDevice(nvrhi::IDevice *device) : m_Device(device)
    {
    }

    std::shared_ptr<Mesh> CreateMesh(const std::vector<Vertex> &vertices,
                                     const std::vector<uint32_t> &indices, std::string &error,
                                     nvrhi::PrimitiveType primitiveType = nvrhi::PrimitiveType::TriangleList) const;
    std::shared_ptr<TextureResource> CreateTexture(const Engine::ImportedImage &image, bool srgb,
                                                   std::string &error,
                                                   const Engine::ImportedTexture *sampler = nullptr) const;

  private:
    nvrhi::DeviceHandle m_Device;
};
} // namespace Engine::Renderer
