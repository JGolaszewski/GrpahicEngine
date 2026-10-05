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
			if (it->first.Guid == guid || it->first.Guid.starts_with(guid + "/")) {
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

	bool ResourceManager::GetOrCreateModelMeshes(
		const std::string& guid,
		uint64_t generation,
		const Engine::ImportedModel& model,
		std::vector<std::vector<std::shared_ptr<Mesh>>>& meshes,
		std::string& error) {
		error.clear();
		meshes.clear();
		if (guid.empty() || model.meshes.empty()) {
			error = "model upload requires an asset GUID and at least one mesh";
			return false;
		}

		std::vector<std::pair<Key, std::shared_ptr<Mesh>>> staged;
		meshes.resize(model.meshes.size());
		for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
			const auto& sourceMesh = model.meshes[meshIndex];
			if (sourceMesh.primitives.empty()) {
				error = "model mesh " + std::to_string(meshIndex) + " contains no primitives";
				break;
			}
			meshes[meshIndex].reserve(sourceMesh.primitives.size());
			for (size_t primitiveIndex = 0; primitiveIndex < sourceMesh.primitives.size(); ++primitiveIndex) {
				const std::string subresource = "mesh/" + std::to_string(meshIndex) + "/primitive/" + std::to_string(primitiveIndex);
				const Key key{guid + "/" + subresource, generation};
				if (const auto cached = m_Meshes.find(key); cached != m_Meshes.end()) {
					meshes[meshIndex].push_back(cached->second);
					continue;
				}
				const auto& primitive = sourceMesh.primitives[primitiveIndex];
				auto gpuMesh = m_RenderDevice.CreateMesh(primitive.vertices, primitive.indices, error);
				if (!gpuMesh) break;
				staged.emplace_back(key, gpuMesh);
				meshes[meshIndex].push_back(std::move(gpuMesh));
			}
			if (!error.empty()) break;
		}
		if (!error.empty()) {
			for (auto& [key, mesh] : staged) {
				(void)key;
				Retire(std::move(mesh));
			}
			meshes.clear();
			return false;
		}

		for (auto it = m_Meshes.begin(); it != m_Meshes.end();) {
			if ((it->first.Guid == guid || it->first.Guid.starts_with(guid + "/")) &&
				it->first.Generation != generation) {
				Retire(std::move(it->second));
				it = m_Meshes.erase(it);
			} else {
				++it;
			}
		}
		for (auto& [key, mesh] : staged) {
			m_Meshes.emplace(std::move(key), std::move(mesh));
		}
		return true;
	}

	void ResourceManager::Release(const std::string& guid) {
		for (auto it = m_Meshes.begin(); it != m_Meshes.end();) {
			if (it->first.Guid == guid || it->first.Guid.starts_with(guid + "/")) {
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
