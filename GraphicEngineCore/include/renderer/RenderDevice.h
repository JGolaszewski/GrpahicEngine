#pragma once

#include "CoreExport.h"
#include "renderer/Mesh.h"
#include "AssetImporter.h"

#include <nvrhi/nvrhi.h>
#include <memory>
#include <string>
#include <vector>

namespace Renderer {
	struct TextureResource {
		nvrhi::TextureHandle Texture;
		nvrhi::SamplerHandle Sampler;
	};

	class ENGINE_API RenderDevice {
	public:
		explicit RenderDevice(nvrhi::IDevice* device) : m_Device(device) {}

		std::shared_ptr<Mesh> CreateMesh(
			const std::vector<Vertex>& vertices,
			const std::vector<uint32_t>& indices,
			std::string& error) const;
		std::shared_ptr<TextureResource> CreateTexture(
			const Engine::ImportedImage& image,
			bool srgb,
			std::string& error) const;

	private:
		nvrhi::DeviceHandle m_Device;
	};
}
