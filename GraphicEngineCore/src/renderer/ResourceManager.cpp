#include "renderer/ResourceManager.h"

#include <functional>

namespace Renderer {
	ResourceManager::ResourceManager(nvrhi::IDevice* device)
		: m_Device(device), m_RenderDevice(device) {}

	ResourceManager::~ResourceManager() {
		if (m_Device) m_Device->waitForIdle();
		m_Meshes.clear();
		m_RetiredMeshes.clear();
	}

	size_t ResourceManager::KeyHash::operator()(const Key& key) const noexcept {
		const size_t guidHash = std::hash<std::string>{}(key.Guid);
		const size_t generationHash = std::hash<uint64_t>{}(key.Generation);
		return guidHash ^ (generationHash + 0x9e3779b9 + (guidHash << 6) + (guidHash >> 2));
	}

	std::shared_ptr<Mesh> ResourceManager::GetOrCreateMesh(
		const std::string& guid,
		uint64_t generation,
		const std::vector<Vertex>& vertices,
		const std::vector<uint32_t>& indices,
		std::string& error) {
		error.clear();
		if (guid.empty()) {
			error = "mesh cache requires a non-empty asset GUID";
			return nullptr;
		}
		const Key key{guid, generation};
		if (const auto it = m_Meshes.find(key); it != m_Meshes.end()) return it->second;

		auto mesh = m_RenderDevice.CreateMesh(vertices, indices, error);
		if (!mesh) return nullptr;
		for (auto it = m_Meshes.begin(); it != m_Meshes.end();) {
			if (it->first.Guid == guid) {
				Retire(std::move(it->second));
				it = m_Meshes.erase(it);
			} else {
				++it;
			}
		}
		m_Meshes.emplace(key, mesh);
		return mesh;
	}

	std::shared_ptr<Mesh> ResourceManager::GetMesh(const std::string& guid, uint64_t generation) const {
		const auto it = m_Meshes.find(Key{guid, generation});
		return it == m_Meshes.end() ? nullptr : it->second;
	}

	void ResourceManager::Release(const std::string& guid) {
		for (auto it = m_Meshes.begin(); it != m_Meshes.end();) {
			if (it->first.Guid == guid) {
				Retire(std::move(it->second));
				it = m_Meshes.erase(it);
			} else {
				++it;
			}
		}
	}

	void ResourceManager::Retire(std::shared_ptr<Mesh> mesh) {
		if (mesh) m_RetiredMeshes.push_back({m_SubmittedFrames + FramesInFlight, std::move(mesh)});
	}

	void ResourceManager::AdvanceSubmittedFrame() {
		++m_SubmittedFrames;
		if (m_RetiredMeshes.empty() || m_RetiredMeshes.front().RetireAfterFrame > m_SubmittedFrames) return;
		if (!m_Device || !m_Device->waitForIdle()) return;
		while (!m_RetiredMeshes.empty() && m_RetiredMeshes.front().RetireAfterFrame <= m_SubmittedFrames) {
			m_RetiredMeshes.pop_front();
		}
	}
}
