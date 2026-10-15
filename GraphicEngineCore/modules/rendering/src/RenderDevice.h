#pragma once

#include "assets/include/AssetImporter.h"
#include "runtime/include/CoreExport.h"
#include "rendering/include/renderer/Mesh.h"
#include "rendering/include/renderer/TextureResource.h"

#include <memory>
#include <nvrhi/nvrhi.h>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{
class ENGINE_API RenderDevice
{
  public:
    explicit RenderDevice(nvrhi::IDevice *device) : m_Device(device)
    {
    }

    std::shared_ptr<Mesh> CreateMesh(const std::vector<Vertex> &vertices,
                                     const std::vector<uint32_t> &indices, std::string &error,
                                     PrimitiveTopology topology = PrimitiveTopology::Triangles) const;
    std::shared_ptr<TextureResource> CreateTexture(const Engine::ImportedImage &image, bool srgb,
                                                   std::string &error,
                                                   const Engine::ImportedTexture *sampler = nullptr) const;

  private:
    nvrhi::DeviceHandle m_Device;
};
} // namespace Engine::Renderer

#ifdef _MSC_VER
#pragma warning(pop)
#endif
