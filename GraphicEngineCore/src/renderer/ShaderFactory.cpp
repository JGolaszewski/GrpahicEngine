#include "renderer/ShaderFactory.h"

#include <iostream>
#include <fstream>
#include <cstdlib>

namespace Renderer {

	ShaderFactory::ShaderFactory(nvrhi::IDevice* device) 
		: m_Device(device) {
		if (const char* configuredRoot = std::getenv("GRAPHIC_ENGINE_ASSET_ROOT")) {
			m_AssetRoot = configuredRoot;
		}
		else {
			#ifdef ENGINE_DEFAULT_ASSET_ROOT
			m_AssetRoot = ENGINE_DEFAULT_ASSET_ROOT;
			#else
			m_AssetRoot = std::filesystem::current_path();
			#endif
		}
	}

	nvrhi::ShaderHandle ShaderFactory::GetShader(const std::string& path, nvrhi::ShaderType type) {
		const std::filesystem::path requestedPath(path);
		const std::filesystem::path resolvedPath = requestedPath.is_absolute()
			? requestedPath
			: m_AssetRoot / requestedPath;
		const std::string cacheKey = resolvedPath.lexically_normal().string();
		if(m_shaderCache.count(cacheKey) > 0) {
			return m_shaderCache[cacheKey];
		}

		auto binary = LoadShaderBinary(cacheKey);
		if(binary.empty()) {
			std::cerr << "ERROR: Could not load shader: '" << cacheKey << "'." << std::endl;
			return nullptr;
		}

		nvrhi::ShaderDesc desc(type);
		desc.debugName = cacheKey;

		auto handle = m_Device->createShader(desc, binary.data(), binary.size());

		if(handle) {
			m_shaderCache[cacheKey] = handle;
		}
		return handle;
	}

	std::vector<char> ShaderFactory::LoadShaderBinary(const std::string& path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if(!file.is_open()) return {};

		std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);

		std::vector<char> buffer(size);
		if(file.read(buffer.data(), size)) {
			return buffer;
		}
		return {};
	}
}
