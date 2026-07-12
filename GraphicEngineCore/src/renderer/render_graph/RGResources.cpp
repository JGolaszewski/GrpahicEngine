#include "RGResources.h"

namespace Renderer {
	bool RGTextureDescEqual::operator()(const RGTextureDesc& a, const RGTextureDesc& b) const {
		return a.width == b.width && a.height == b.height;
	}

	std::size_t RGTextureDescHasher::operator()(const RGTextureDesc& desc) const {
		return std::hash<uint32_t>()(desc.width) ^ (std::hash<uint32_t>()(desc.height) << 1);
	}
}