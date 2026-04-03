#include "renderer/ShaderFactory.h"

#include <iostream>
#include <fstream>

namespace Renderer {

	ShaderFactory::ShaderFactory(nvrhi::IDevice* device) 
		: m_Device(device) {}

	nvrhi::ShaderHandle ShaderFactory::GetShader(const std::string& path, nvrhi::ShaderType type) {
		if(m_shaderCache.count(path) > 0) {
			return m_shaderCache[path];
		}

		auto binary = LoadShaderBinary(path);
		if(binary.empty()) {
			std::cerr << "ERROR: Could not load shader: '" << path << "'." << std::endl;
			return nullptr;
		}

		nvrhi::ShaderDesc desc(type);
		desc.debugName = path;

		auto handle = m_Device->createShader(desc, binary.data(), binary.size());

		if(handle) {
			m_shaderCache[path] = handle;
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