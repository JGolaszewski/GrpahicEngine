#pragma once

#include "CoreExport.h"
#include "AssetImporter.h"
#include "renderer/RenderDevice.h"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Renderer {
	struct UploadProgress {
		uint64_t BytesQueued = 0;
		uint64_t BytesSubmitted = 0;
		uint64_t BytesCompleted = 0;
		uint32_t BatchesSubmitted = 0;
		uint32_t FailedBatches = 0;
		std::string LastError;
	};

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
		bool FlushUploads(std::string& error);
		UploadProgress GetUploadProgress() const { return m_UploadProgress; }
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
		struct PendingUpload {
			std::function<void(nvrhi::ICommandList*)> Record;
			uint64_t Bytes = 0;
		};
		struct InFlightUploadBatch {
			nvrhi::EventQueryHandle CompletionQuery;
			std::vector<PendingUpload> Staging;
		};
		void QueueMeshUpload(const std::shared_ptr<Mesh>& mesh,
			std::vector<Vertex> vertices, std::vector<uint32_t> indices);
		void QueueTextureUpload(const std::shared_ptr<TextureResource>& texture,
			const Engine::ImportedImage& image);
		void RetireCompletedUploads();
		void Retire(std::shared_ptr<Mesh> mesh);
		void Retire(std::shared_ptr<TextureResource> texture);

		nvrhi::DeviceHandle m_Device;
		RenderDevice m_RenderDevice;
		std::unordered_map<Key, std::shared_ptr<Mesh>, KeyHash> m_Meshes;
		std::unordered_map<Key, std::shared_ptr<TextureResource>, KeyHash> m_Textures;
		std::deque<RetiredMesh> m_RetiredMeshes;
		std::deque<RetiredTexture> m_RetiredTextures;
		std::vector<PendingUpload> m_PendingUploads;
		std::deque<InFlightUploadBatch> m_InFlightUploads;
		UploadProgress m_UploadProgress;
		std::shared_ptr<TextureResource> m_FallbackColor;
		std::shared_ptr<TextureResource> m_FallbackNormal;
		uint64_t m_SubmittedFrames = 0;
		uint64_t m_CompletedFrames = 0;
	};
}
