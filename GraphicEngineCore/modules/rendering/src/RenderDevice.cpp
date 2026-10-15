#include "rendering/src/RenderDevice.h"
#include "MeshBackend.h"
#include "TextureBackend.h"

#include <algorithm>
#include <limits>

namespace Engine::Renderer {
auto RenderDevice::CreateMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                              std::string& error, nvrhi::PrimitiveType primitiveType) const -> std::shared_ptr<Mesh> {
  error.clear();
  if ((m_Device == nullptr) || vertices.empty() || vertices.size() > std::numeric_limits<uint32_t>::max() ||
      indices.size() > std::numeric_limits<uint32_t>::max()) {
    error = "mesh creation requires a valid device, non-empty vertices, and counts that fit the renderer "
            "limits";
    return nullptr;
  }
  for (const uint32_t index : indices) {
    if (index >= vertices.size()) {
      error = "mesh index references a vertex outside the supplied vertex data";
      return nullptr;
    }
  }
  const PrimitiveTopology topology = primitiveType == nvrhi::PrimitiveType::LineList    ? PrimitiveTopology::Lines
                                     : primitiveType == nvrhi::PrimitiveType::LineStrip ? PrimitiveTopology::LineStrip
                                     : primitiveType == nvrhi::PrimitiveType::PointList ? PrimitiveTopology::Points
                                                                                        : PrimitiveTopology::Triangles;
  auto mesh = MeshBackendAccess::Create(m_Device, vertices, indices, topology);
  if (!mesh || !mesh->IsValid()) {
    error = "NVRHI failed to create mesh buffers";
    return nullptr;
  }
  return mesh;
}

auto RenderDevice::CreateTexture(const Engine::ImportedImage& image, bool srgb, std::string& error,
                                 const Engine::ImportedTexture* sampler) const -> std::shared_ptr<TextureResource> {
  error.clear();
  if ((m_Device == nullptr) || image.width == 0 || image.height == 0 ||
      static_cast<size_t>(image.width) > std::numeric_limits<size_t>::max() / image.height / 4 ||
      image.rgba8.size() != static_cast<size_t>(image.width) * image.height * 4) {
    error = "texture creation requires valid RGBA8 pixels and a renderer device";
    return nullptr;
  }
  auto resource = std::make_shared<TextureResource>();
  nvrhi::TextureDesc textureDesc;
  uint32_t mipLevels = 1;
  for (uint32_t dimension = (std::max)(image.width, image.height); dimension > 1; dimension >>= 1) {
    ++mipLevels;
  }
  textureDesc.setDimension(nvrhi::TextureDimension::Texture2D)
      .setWidth(image.width)
      .setHeight(image.height)
      .setDepth(1)
      .setArraySize(1)
      .setMipLevels(mipLevels)
      .setFormat(srgb ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM)
      .setDebugName(image.name.empty() ? "AssetTexture" : image.name)
      .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
  auto texture = m_Device->createTexture(textureDesc);
  TextureBackendAccess::SetTexture(*resource, texture);
  if (texture == nullptr) {
    error = "NVRHI failed to create a texture resource";
    return nullptr;
  }

  const auto addressMode = [](int value) -> nvrhi::SamplerAddressMode {
    switch (value) {
    case 33071:
      return nvrhi::SamplerAddressMode::Clamp;
    case 33648:
      return nvrhi::SamplerAddressMode::Mirror;
    default:
      return nvrhi::SamplerAddressMode::Wrap;
    }
  };
  const int minFilter = (sampler != nullptr) ? sampler->minFilter : -1;
  const int magFilter = (sampler != nullptr) ? sampler->magFilter : -1;
  const bool minLinear = minFilter != 9728 && minFilter != 9984 && minFilter != 9986;
  const bool magLinear = magFilter != 9728;
  const bool mipLinear = minFilter == 9986 || minFilter == 9987;
  nvrhi::SamplerDesc samplerDesc;
  samplerDesc.setAddressU(addressMode((sampler != nullptr) ? sampler->wrapS : 10497))
      .setAddressV(addressMode((sampler != nullptr) ? sampler->wrapT : 10497))
      .setAddressW(nvrhi::SamplerAddressMode::Wrap)
      .setMinFilter(minLinear)
      .setMagFilter(magLinear)
      .setMipFilter(mipLinear);
  auto createdSampler = m_Device->createSampler(samplerDesc);
  TextureBackendAccess::SetSampler(*resource, createdSampler);
  if (createdSampler == nullptr) {
    error = "NVRHI failed to create a texture sampler";
    return nullptr;
  }

  return resource;
}
} // namespace Engine::Renderer
