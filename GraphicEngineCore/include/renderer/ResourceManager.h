#pragma once

#include "CoreExport.h"
#include "AssetImporter.h"
#include "renderer/RenderDevice.h"

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

namespace Renderer {
	class ENGINE_API ResourceManager {
	public:
		static constexpr uint64_t FramesInFlight = 2;

		explicit ResourceManager(nvrhi::IDevice* device);
		~ResourceManager();

		std::shared_ptr<Mesh> GetOrCreateMesh(
			const std::string& guid,
			uint64_t generation,
			const std::vector<Vertex>& vertices,
			const std::vector<uint32_t>& indices,
			std::string& error);
		bool GetOrCreateModelMeshes(
			const std::string& guid,
			uint64_t generation,
			const ImportedModel& model,
			std::vector<std::vector<std::shared_ptr<Mesh>>>& meshes,
			std::string& error);
		std::shared_ptr<Mesh> GetMesh(const std::string& guid, uint64_t generation) const;
		void Release(const std::string& guid);
		void AdvanceSubmittedFrame();

	private:
		struct Key {
			std::string Guid;
			uint64_t Generation = 0;
			bool operator==(const Key&) const = default;
		};
		struct KeyHash {
			size_t operator()(const Key& key) const noexcept;
		};
		struct RetiredMesh {
			uint64_t RetireAfterFrame = 0;
			std::shared_ptr<Mesh> Resource;
		};

		void Retire(std::shared_ptr<Mesh> mesh);

		nvrhi::DeviceHandle m_Device;
		RenderDevice m_RenderDevice;
		std::unordered_map<Key, std::shared_ptr<Mesh>, KeyHash> m_Meshes;
		std::deque<RetiredMesh> m_RetiredMeshes;
		uint64_t m_SubmittedFrames = 0;
	};
}
