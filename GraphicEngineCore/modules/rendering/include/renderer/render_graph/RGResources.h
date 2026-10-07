#pragma once

#include <cassert>
#include <span>
#include <unordered_map>
#include <vector>

#include <nvrhi/nvrhi.h>

#include "runtime/include/CoreExport.h"

namespace Engine::Renderer
{

struct RGResourceID
{
    uint32_t value = UINT32_MAX;

    inline bool isValid() const
    {
        return value != UINT32_MAX;
    }
    inline bool operator==(const RGResourceID &other) const
    {
        return value == other.value;
    }
    inline bool operator!=(const RGResourceID &other) const
    {
        return !(*this == other);
    }
};

struct RGTextureAccess
{
    RGResourceID resource;
    nvrhi::ResourceStates state = nvrhi::ResourceStates::Unknown;
    nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources;
    bool read = false;
    bool write = false;
};

struct RGTextureDesc
{
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t arraySize = 1;

    uint32_t mipLevels = 1;
    bool isRenderTarget = false;
    bool isShaderResource = true;
    bool isUnorderedAccess = false;

    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    nvrhi::TextureDimension dimension = nvrhi::TextureDimension::Texture2D;
};

struct RGTextureDescHasher
{
    std::size_t operator()(const RGTextureDesc &desc) const
    {
        std::size_t hash = 17;

        auto combine = [&hash](std::size_t v) { hash ^= v + 0x9e3779b9 + (hash << 6) + (hash >> 2); };

        combine(std::hash<uint32_t>()(desc.width));
        combine(std::hash<uint32_t>()(desc.height));
        combine(std::hash<uint32_t>()(desc.arraySize));
        combine(std::hash<uint32_t>()(desc.mipLevels));
        combine(std::hash<bool>()(desc.isRenderTarget));
        combine(std::hash<bool>()(desc.isShaderResource));
        combine(std::hash<bool>()(desc.isUnorderedAccess));
        combine(std::hash<uint32_t>()(static_cast<uint32_t>(desc.format)));
        combine(std::hash<uint32_t>()(static_cast<uint32_t>(desc.dimension)));

        return hash;
    }
};

struct RGTextureDescEqual
{
    bool operator()(const RGTextureDesc &a, const RGTextureDesc &b) const
    {
        return a.width == b.width && a.height == b.height && a.arraySize == b.arraySize &&
               a.mipLevels == b.mipLevels && a.isRenderTarget == b.isRenderTarget &&
               a.isShaderResource == b.isShaderResource && a.isUnorderedAccess == b.isUnorderedAccess &&
               a.format == b.format && a.dimension == b.dimension;
    }
};

struct RGResource
{
    RGTextureDesc desc;
    bool isImported = false;
    nvrhi::ResourceStates initialState = nvrhi::ResourceStates::Common;
    nvrhi::ResourceStates finalState = nvrhi::ResourceStates::Common;

    uint32_t firstPassUse = UINT32_MAX;
    uint32_t lastPassUse = 0;

    nvrhi::TextureHandle physicalHandle = nullptr;
};

class ENGINE_API RGResourceRegistry
{
  public:
    RGResourceID CreateResource(const RGTextureDesc &desc);
    RGResourceID ImportResource(const RGTextureDesc &desc, nvrhi::TextureHandle texture,
                                nvrhi::ResourceStates initialState, nvrhi::ResourceStates finalState);
    bool IsValid(RGResourceID id) const noexcept;
    inline RGResource &GetResource(RGResourceID id)
    {
        assert(id.isValid() && id.value < m_Registry.size());
        return m_Registry[id.value];
    }

    void Clear()
    {
        m_Registry.clear();
    }

    inline std::span<RGResource> GetAllResources()
    {
        return m_Registry;
    }
    inline std::span<const RGResource> GetAllResources() const
    {
        return m_Registry;
    }

  private:
    std::vector<RGResource> m_Registry;
};
} // namespace Engine::Renderer
