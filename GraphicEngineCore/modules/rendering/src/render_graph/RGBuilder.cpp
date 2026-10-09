#include "rendering/include/renderer/render_graph/RGBuilder.h"

#include <algorithm>
#include <utility>

namespace
{
bool RangesOverlap(uint32_t baseA, uint32_t countA, uint32_t baseB, uint32_t countB, uint32_t all)
{
    const uint64_t endA = countA == all ? UINT64_MAX : static_cast<uint64_t>(baseA) + countA;
    const uint64_t endB = countB == all ? UINT64_MAX : static_cast<uint64_t>(baseB) + countB;
    return baseA < endB && baseB < endA;
}

bool Overlaps(const nvrhi::TextureSubresourceSet &a, const nvrhi::TextureSubresourceSet &b)
{
    return RangesOverlap(a.baseMipLevel, a.numMipLevels, b.baseMipLevel, b.numMipLevels,
                         nvrhi::TextureSubresourceSet::AllMipLevels) &&
           RangesOverlap(a.baseArraySlice, a.numArraySlices, b.baseArraySlice, b.numArraySlices,
                         nvrhi::TextureSubresourceSet::AllArraySlices);
}
} // namespace

namespace Engine::Renderer
{
RGBuilder::RGBuilder(RGResourceRegistry &registry, RGPassDependencies &passDeps)
    : m_Registry(registry), m_CurrentPassDeps(passDeps)
{
}

RGResourceID RGBuilder::CreateTexture(const RGTextureDesc &desc)
{
    if (desc.width == 0 || desc.height == 0 || desc.arraySize == 0 || desc.mipLevels == 0 ||
        desc.format == nvrhi::Format::UNKNOWN ||
        (!desc.isRenderTarget && !desc.isShaderResource && !desc.isUnorderedAccess))
    {
        m_CurrentPassDeps.valid = false;
        m_CurrentPassDeps.error = "Texture descriptor has invalid dimensions, format, or capabilities";
        return {};
    }
    return m_Registry.CreateResource(desc);
}

RGResourceID RGBuilder::ImportTexture(const RGTextureDesc &desc, nvrhi::TextureHandle texture,
                                      nvrhi::ResourceStates initialState, nvrhi::ResourceStates finalState)
{
    if (!texture || initialState == nvrhi::ResourceStates::Unknown ||
        finalState == nvrhi::ResourceStates::Unknown)
    {
        m_CurrentPassDeps.valid = false;
        m_CurrentPassDeps.error = "Imported textures require a texture and explicit initial and final states";
        return {};
    }
    const nvrhi::TextureDesc &physical = texture->getDesc();
    if (desc.width != physical.width || desc.height != physical.height ||
        desc.arraySize != physical.arraySize || desc.mipLevels != physical.mipLevels ||
        desc.format != physical.format || desc.dimension != physical.dimension ||
        desc.isRenderTarget != physical.isRenderTarget ||
        desc.isShaderResource != physical.isShaderResource || desc.isUnorderedAccess != physical.isUAV)
    {
        m_CurrentPassDeps.valid = false;
        m_CurrentPassDeps.error = "Imported texture descriptor does not match the physical texture";
        return {};
    }
    return m_Registry.ImportResource(desc, std::move(texture), initialState, finalState);
}

void RGBuilder::DeclareAccess(RGResourceID id, nvrhi::ResourceStates state,
                              nvrhi::TextureSubresourceSet subresources, bool read, bool write)
{
    if (!m_Registry.IsValid(id))
    {
        m_CurrentPassDeps.valid = false;
        m_CurrentPassDeps.error = "Access references an invalid resource ID";
        return;
    }
    const RGResource &resource = m_Registry.GetResource(id);
    if (state == nvrhi::ResourceStates::Unknown && write)
    {
        const bool depth =
            resource.desc.format == nvrhi::Format::D32 || resource.desc.format == nvrhi::Format::D24S8 ||
            resource.desc.format == nvrhi::Format::D16 || resource.desc.format == nvrhi::Format::D32S8;
        state = depth ? nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::RenderTarget;
    }
    const auto hasState = [state](nvrhi::ResourceStates value) {
        return (static_cast<uint32_t>(state) & static_cast<uint32_t>(value)) != 0;
    };
    const bool shaderRead = hasState(nvrhi::ResourceStates::ShaderResource);
    const bool depthFormat =
        resource.desc.format == nvrhi::Format::D32 || resource.desc.format == nvrhi::Format::D24S8 ||
        resource.desc.format == nvrhi::Format::D16 || resource.desc.format == nvrhi::Format::D32S8;
    const bool renderTarget = hasState(nvrhi::ResourceStates::RenderTarget);
    const bool depthWrite = hasState(nvrhi::ResourceStates::DepthWrite);
    const bool unorderedAccess = hasState(nvrhi::ResourceStates::UnorderedAccess);
    const bool copySource = hasState(nvrhi::ResourceStates::CopySource);
    const bool copyDestination = hasState(nvrhi::ResourceStates::CopyDest);
    const bool depthRead = hasState(nvrhi::ResourceStates::DepthRead);
    const bool readable = shaderRead || copySource || depthRead || unorderedAccess;
    const bool writable = renderTarget || depthWrite || copyDestination || unorderedAccess;
    if (state == nvrhi::ResourceStates::Unknown || (read && !readable) || (write && !writable) ||
        (read && shaderRead && !resource.desc.isShaderResource) ||
        (write && renderTarget && (!resource.desc.isRenderTarget || depthFormat)) ||
        (write && depthWrite && (!resource.desc.isRenderTarget || !depthFormat)) ||
        (read && depthRead && !depthFormat) ||
        ((read || write) && unorderedAccess && !resource.desc.isUnorderedAccess))
    {
        m_CurrentPassDeps.valid = false;
        m_CurrentPassDeps.error = "Access state is incompatible with the texture descriptor";
        return;
    }
    const auto validRange = [](uint32_t base, uint32_t count, uint32_t limit, uint32_t all) {
        return count != 0 && base < limit && (count == all || count <= limit - base);
    };
    if (!validRange(subresources.baseMipLevel, subresources.numMipLevels, resource.desc.mipLevels,
                    nvrhi::TextureSubresourceSet::AllMipLevels) ||
        !validRange(subresources.baseArraySlice, subresources.numArraySlices, resource.desc.arraySize,
                    nvrhi::TextureSubresourceSet::AllArraySlices))
    {
        m_CurrentPassDeps.valid = false;
        m_CurrentPassDeps.error = "Access references a subresource outside the texture descriptor";
        return;
    }
    if (read && std::find(m_CurrentPassDeps.inputs.begin(), m_CurrentPassDeps.inputs.end(), id) ==
                    m_CurrentPassDeps.inputs.end())
    {
        m_CurrentPassDeps.inputs.push_back(id);
    }
    for (const RGTextureAccess &existing : m_CurrentPassDeps.accesses)
    {
        if (existing.resource == id && (existing.write || write) &&
            Overlaps(existing.subresources, subresources))
        {
            m_CurrentPassDeps.valid = false;
            m_CurrentPassDeps.error = "Overlapping read/write declarations require one ReadWrite declaration";
            return;
        }
    }
    if (write)
        m_CurrentPassDeps.outputs.push_back(id);
    m_CurrentPassDeps.accesses.push_back({id, state, subresources, read, write});
}

void RGBuilder::Read(RGResourceID id, nvrhi::ResourceStates state, nvrhi::TextureSubresourceSet subresources)
{
    DeclareAccess(id, state, subresources, true, false);
}

void RGBuilder::Write(RGResourceID id, nvrhi::ResourceStates state, nvrhi::TextureSubresourceSet subresources)
{
    DeclareAccess(id, state, subresources, false, true);
}

void RGBuilder::ReadWrite(RGResourceID id, nvrhi::ResourceStates state,
                          nvrhi::TextureSubresourceSet subresources)
{
    DeclareAccess(id, state, subresources, true, true);
}

RGResourceID RGBuilder::CreateAndWriteTexture(const RGTextureDesc &desc)
{
    RGResourceID newId = CreateTexture(desc);
    Write(newId);
    return newId;
}

} // namespace Engine::Renderer
