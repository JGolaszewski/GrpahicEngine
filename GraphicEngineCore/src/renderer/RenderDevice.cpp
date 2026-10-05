#include "renderer/RenderDevice.h"

#include <limits>

namespace Renderer {
	std::shared_ptr<Mesh> RenderDevice::CreateMesh(
		const std::vector<Vertex>& vertices,
		const std::vector<uint32_t>& indices,
		std::string& error) const {
		error.clear();
		if (!m_Device || vertices.empty() || vertices.size() > std::numeric_limits<uint32_t>::max() ||
			indices.size() > std::numeric_limits<uint32_t>::max()) {
			error = "mesh creation requires a valid device, non-empty vertices, and counts that fit the renderer limits";
			return nullptr;
		}
		for (const uint32_t index : indices) {
			if (index >= vertices.size()) {
				error = "mesh index references a vertex outside the supplied vertex data";
				return nullptr;
			}
		}
		auto mesh = std::make_shared<Mesh>(m_Device, vertices, indices);
		if (!mesh->IsValid()) {
			error = "NVRHI failed to create mesh buffers";
			return nullptr;
		}
		nvrhi::CommandListParameters parameters;
		parameters.setEnableImmediateExecution(true);
		auto commandList = m_Device->createCommandList(parameters);
		if (!commandList) {
			error = "NVRHI failed to create an upload command list";
			return nullptr;
		}
		commandList->open();
		mesh->UploadData(commandList, vertices, indices);
		commandList->close();
		m_Device->executeCommandList(commandList);
		return mesh;
	}
}
