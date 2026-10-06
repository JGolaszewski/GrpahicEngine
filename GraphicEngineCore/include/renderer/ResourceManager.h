#pragma once

#include "CoreExport.h"
#include "AssetImporter.h"
#include "renderer/RenderDevice.h"

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
			const Engine::ImportedModel& model,
			std::vector<std::vector<std::shared_ptr<Mesh>>>& meshes,
			std::string& error);
		std::shared_ptr<Mesh> GetMesh(const std::string& guid, uint64_t generation) const;
		std::shared_ptr<TextureResource> GetOrCreateTexture(
			const std::string& identity, uint64_t generation, const Engine::ImportedImage& image,
			bool srgb, std::string& error);
		std::shared_ptr<TextureResource> GetFallbackTexture(bool srgb) const;
		void Release(const std::string& guid);
		void AdvanceSubmittedFrame(uint64_t submittedFrame, uint64_t completedFrame);

	private:
		struct Key {
			std::string Guid;
			uint64_t Generation = 0;
			bool Srgb = false;
			bool operator==(const Key&) const = default;
		};
		struct KeyHash {
			size_t operator()(const Key& key) const noexcept;
		};
		struct RetiredMesh {
			uint64_t RetireAfterFrame = 0;
			std::shared_ptr<Mesh> Resource;
		};
		struct RetiredTexture {
			uint64_t RetireAfterFrame = 0;
			std::shared_ptr<TextureResource> Resource;
		};
		void Retire(std::shared_ptr<Mesh> mesh);
		void Retire(std::shared_ptr<TextureResource> texture);

		nvrhi::DeviceHandle m_Device;
		RenderDevice m_RenderDevice;
		std::unordered_map<Key, std::shared_ptr<Mesh>, KeyHash> m_Meshes;
		std::unordered_map<Key, std::shared_ptr<TextureResource>, KeyHash> m_Textures;
		std::deque<RetiredMesh> m_RetiredMeshes;
		std::deque<RetiredTexture> m_RetiredTextures;
		std::shared_ptr<TextureResource> m_FallbackColor;
		std::shared_ptr<TextureResource> m_FallbackNormal;
		uint64_t m_SubmittedFrames = 0;
		uint64_t m_CompletedFrames = 0;
	};
}
