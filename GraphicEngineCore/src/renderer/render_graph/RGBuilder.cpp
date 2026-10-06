#include "renderer/render_graph/RGBuilder.h"

#include <algorithm>
#include <utility>

namespace Renderer {
	RGBuilder::RGBuilder(RGResourceRegistry& registry, RGPassDependencies& passDeps)
		: m_Registry(registry), m_CurrentPassDeps(passDeps) {
	}

	RGResourceID RGBuilder::CreateTexture(const RGTextureDesc& desc) {
		return m_Registry.CreateResource(desc);
	}

	RGResourceID RGBuilder::ImportTexture(const RGTextureDesc& desc, nvrhi::TextureHandle texture,
		nvrhi::ResourceStates initialState, nvrhi::ResourceStates finalState) {
		if (initialState == nvrhi::ResourceStates::Unknown || finalState == nvrhi::ResourceStates::Unknown) {
			m_CurrentPassDeps.valid = false;
			m_CurrentPassDeps.error = "Imported textures require explicit initial and final states";
			return {};
		}
		return m_Registry.ImportResource(desc, std::move(texture), initialState, finalState);
	}

	void RGBuilder::DeclareAccess(RGResourceID id, nvrhi::ResourceStates state,
		nvrhi::TextureSubresourceSet subresources, bool read, bool write) {
		if (!m_Registry.IsValid(id)) {
			m_CurrentPassDeps.valid = false;
			m_CurrentPassDeps.error = "Access references an invalid resource ID";
			return;
		}
		const RGResource& resource = m_Registry.GetResource(id);
		if (state == nvrhi::ResourceStates::Unknown && write) {
			const bool depth = resource.desc.format == nvrhi::Format::D32 || resource.desc.format == nvrhi::Format::D24S8 ||
				resource.desc.format == nvrhi::Format::D16 || resource.desc.format == nvrhi::Format::D32S8;
			state = depth ? nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::RenderTarget;
		}
		const auto hasState = [state](nvrhi::ResourceStates value) {
			return (static_cast<uint32_t>(state) & static_cast<uint32_t>(value)) != 0;
		};
		const bool shaderRead = hasState(nvrhi::ResourceStates::ShaderResource);
		const bool depthFormat = resource.desc.format == nvrhi::Format::D32 || resource.desc.format == nvrhi::Format::D24S8 ||
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
			((read || write) && unorderedAccess && !resource.desc.isUnorderedAccess)) {
			m_CurrentPassDeps.valid = false;
			m_CurrentPassDeps.error = "Access state is incompatible with the texture descriptor";
			return;
		}
		const auto validRange = [](uint32_t base, uint32_t count, uint32_t limit, uint32_t all) {
			return count != 0 && base < limit && (count == all || count <= limit - base);
		};
		if (!validRange(subresources.baseMipLevel, subresources.numMipLevels, resource.desc.mipLevels, nvrhi::TextureSubresourceSet::AllMipLevels) ||
			!validRange(subresources.baseArraySlice, subresources.numArraySlices, resource.desc.arraySize, nvrhi::TextureSubresourceSet::AllArraySlices)) {
			m_CurrentPassDeps.valid = false;
			m_CurrentPassDeps.error = "Access references a subresource outside the texture descriptor";
			return;
		}
		if (read && std::find(m_CurrentPassDeps.inputs.begin(), m_CurrentPassDeps.inputs.end(), id) == m_CurrentPassDeps.inputs.end()) {
			m_CurrentPassDeps.inputs.push_back(id);
		}
		if (write && std::find(m_CurrentPassDeps.outputs.begin(), m_CurrentPassDeps.outputs.end(), id) != m_CurrentPassDeps.outputs.end()) {
			m_CurrentPassDeps.valid = false;
			m_CurrentPassDeps.error = "Resource is written more than once by one pass";
			return;
		}
		if (write) m_CurrentPassDeps.outputs.push_back(id);
		m_CurrentPassDeps.accesses.push_back({ id, state, subresources });
	}

	void RGBuilder::Read(RGResourceID id, nvrhi::ResourceStates state, nvrhi::TextureSubresourceSet subresources) {
		DeclareAccess(id, state, subresources, true, false);
	}

	void RGBuilder::Write(RGResourceID id, nvrhi::ResourceStates state, nvrhi::TextureSubresourceSet subresources) {
		DeclareAccess(id, state, subresources, false, true);
	}

	void RGBuilder::ReadWrite(RGResourceID id, nvrhi::ResourceStates state, nvrhi::TextureSubresourceSet subresources) {
		DeclareAccess(id, state, subresources, true, true);
	}

	RGResourceID RGBuilder::CreateAndWriteTexture(const RGTextureDesc& desc) {
		RGResourceID newId = CreateTexture(desc);
		Write(newId);
		return newId;
	}

}
