#include "rendering/src/ResourceManager.h"
#include "MeshBackend.h"
#include "TextureBackend.h"
#include "rendering/src/TextureMipGenerator.h"
#include "runtime/include/LogMacros.h"

#include <algorithm>
#include <functional>

namespace Engine::Renderer {
namespace {} // namespace

ResourceManager::ResourceManager(nvrhi::IDevice* device) : m_Device(device), m_RenderDevice(device) {
  Engine::ImportedImage color;
  color.name = "FallbackBaseColor";
  color.width = color.height = 1;
  color.rgba8 = {255, 255, 255, 255};
  std::string error;
  m_FallbackColor = m_RenderDevice.CreateTexture(color, true, error);
  if (m_FallbackColor) {
    QueueTextureUpload(m_FallbackColor, color, true, false, false, 0.5F);
  }

  Engine::ImportedImage normal;
  normal.name = "FallbackNormal";
  normal.width = normal.height = 1;
  normal.rgba8 = {128, 128, 255, 255};
  m_FallbackNormal = m_RenderDevice.CreateTexture(normal, false, error);
  if (m_FallbackNormal) {
    QueueTextureUpload(m_FallbackNormal, normal, false, true, false, 0.5F);
  }
}

ResourceManager::~ResourceManager() {
  if (m_Device != nullptr) {
    m_Device->waitForIdle();
  }
  m_PendingUploads.clear();
  m_InFlightUploads.clear();
  m_Meshes.clear();
  m_Textures.clear();
  m_RetiredMeshes.clear();
  m_RetiredTextures.clear();
  m_FallbackColor.reset();
  m_FallbackNormal.reset();
}

auto ResourceManager::KeyHash::operator()(const Key& key) const noexcept -> size_t {
  const size_t guidHash = std::hash<std::string>{}(key.Guid);
  const size_t generationHash = std::hash<uint64_t>{}(key.Generation);
  const size_t combined = guidHash ^ (generationHash + 0x9e3779b9 + (guidHash << 6) + (guidHash >> 2));
  const size_t cutoffHash = std::hash<float>{}(key.AlphaCutoff);
  return combined ^ (key.Srgb ? 0x85ebca6b : 0U) ^ (key.NormalMap ? 0xc2b2ae35 : 0U) ^
         (key.PreserveAlphaCoverage ? 0x27d4eb2f : 0U) ^ cutoffHash;
}

auto ResourceManager::GetOrCreateMesh(const std::string& guid, uint64_t generation, const std::vector<Vertex>& vertices,
                                      const std::vector<uint32_t>& indices, std::string& error)
    -> std::shared_ptr<Mesh> {
  error.clear();
  if (guid.empty()) {
    error = "mesh cache requires a non-empty asset GUID";
    ENGINE_LOG_ERROR("Asset", "Mesh allocation rejected | guid_length: 0 | generation: {}", generation);
    return nullptr;
  }
  const Key key{.Guid = guid, .Generation = generation};
  if (const auto it = m_Meshes.find(key); it != m_Meshes.end()) {
    ENGINE_LOG_TRACE("Asset", "Mesh cache hit | guid: {} | generation: {}", guid, generation);
    return it->second;
  }

  ENGINE_LOG_DEBUG(
      "Asset", "Mesh cache miss | guid: {} | generation: {} | vertex_count: {} | index_count: {} | estimated_bytes: {}",
      guid, generation, vertices.size(), indices.size(),
      (vertices.size() * sizeof(Vertex)) + (indices.size() * sizeof(uint32_t)));
  auto mesh = m_RenderDevice.CreateMesh(vertices, indices, error);
  if (!mesh) {
    ENGINE_LOG_ERROR(
        "Render",
        "GPU mesh allocation failed | guid: {} | generation: {} | vertex_count: {} | index_count: {} | error: {}", guid,
        generation, vertices.size(), indices.size(), error);
    return nullptr;
  }
  QueueMeshUpload(mesh, vertices, indices);
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

auto ResourceManager::GetMesh(const std::string& guid, uint64_t generation) const -> std::shared_ptr<Mesh> {
  const auto it = m_Meshes.find(Key{.Guid = guid, .Generation = generation, .Srgb = false});
  return it == m_Meshes.end() ? nullptr : it->second;
}

auto ResourceManager::GetOrCreateTexture(const std::string& identity, uint64_t generation,
                                         const Engine::ImportedImage& image, bool srgb, std::string& error,
                                         const Engine::ImportedTexture* sampler, bool normalMap,
                                         bool preserveAlphaCoverage, float alphaCutoff)
    -> std::shared_ptr<TextureResource> {
  error.clear();
  if (identity.empty()) {
    error = "texture cache requires a non-empty asset or subresource identity";
    ENGINE_LOG_ERROR("Asset", "Texture allocation rejected | identity_length: 0 | generation: {}", generation);
    return nullptr;
  }
  const Key key{.Guid = identity,
                .Generation = generation,
                .Srgb = srgb,
                .NormalMap = normalMap,
                .PreserveAlphaCoverage = preserveAlphaCoverage,
                .AlphaCutoff = alphaCutoff};
  if (const auto it = m_Textures.find(key); it != m_Textures.end()) {
    ENGINE_LOG_TRACE("Asset", "Texture cache hit | identity: {} | generation: {} | srgb: {}", identity, generation,
                     srgb);
    return it->second;
  }
  ENGINE_LOG_DEBUG(
      "Asset", "Texture cache miss | identity: {} | generation: {} | width: {} | height: {} | byte_size: {} | srgb: {}",
      identity, generation, image.width, image.height, image.rgba8.size(), srgb);
  auto texture = m_RenderDevice.CreateTexture(image, srgb, error, sampler);
  if (!texture) {
    ENGINE_LOG_ERROR(
        "Render", "GPU texture allocation failed | identity: {} | generation: {} | width: {} | height: {} | error: {}",
        identity, generation, image.width, image.height, error);
    return nullptr;
  }
  QueueTextureUpload(texture, image, srgb, normalMap, preserveAlphaCoverage, alphaCutoff);
  for (auto it = m_Textures.begin(); it != m_Textures.end();) {
    if ((it->first.Guid == identity || it->first.Guid.starts_with(identity + "/")) &&
        it->first.Generation != generation) {
      Retire(std::move(it->second));
      it = m_Textures.erase(it);
    } else {
      ++it;
    }
  }
  m_Textures.emplace(key, texture);
  return texture;
}

auto ResourceManager::GetFallbackTexture(bool srgb) const -> std::shared_ptr<TextureResource> {
  return srgb ? m_FallbackColor : m_FallbackNormal;
}

auto ResourceManager::GetOrCreateModelMeshes(const std::string& guid, uint64_t generation,
                                             const Engine::ImportedModel& model,
                                             std::vector<std::vector<std::shared_ptr<Mesh>>>& meshes,
                                             std::string& error) -> bool {
  error.clear();
  meshes.clear();
  if (guid.empty() || model.meshes.empty()) {
    error = "model upload requires an asset GUID and at least one mesh";
    return false;
  }

  struct StagedMesh {
    Key CacheKey;
    std::shared_ptr<Mesh> Resource;
    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;
  };
  std::vector<StagedMesh> staged;
  meshes.resize(model.meshes.size());
  for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
    const auto& sourceMesh = model.meshes.at(meshIndex);
    if (sourceMesh.primitives.empty()) {
      error = "model mesh " + std::to_string(meshIndex) + " contains no primitives";
      break;
    }
    meshes.at(meshIndex).reserve(sourceMesh.primitives.size());
    for (size_t primitiveIndex = 0; primitiveIndex < sourceMesh.primitives.size(); ++primitiveIndex) {
      std::string subresource = "mesh/";
      subresource += std::to_string(meshIndex);
      subresource += "/primitive/";
      subresource += std::to_string(primitiveIndex);
      std::string resourcePath = guid;
      resourcePath += '/';
      resourcePath += subresource;
      const Key key{.Guid = std::move(resourcePath), .Generation = generation};
      if (const auto cached = m_Meshes.find(key); cached != m_Meshes.end()) {
        meshes.at(meshIndex).push_back(cached->second);
        continue;
      }
      const auto& primitive = sourceMesh.primitives.at(primitiveIndex);
      nvrhi::PrimitiveType primitiveType = nvrhi::PrimitiveType::TriangleList;
      switch (primitive.primitiveType) {
      case Engine::ImportedPrimitiveType::Lines:
        primitiveType = nvrhi::PrimitiveType::LineList;
        break;
      case Engine::ImportedPrimitiveType::LineStrip:
        primitiveType = nvrhi::PrimitiveType::LineStrip;
        break;
      case Engine::ImportedPrimitiveType::Points:
        primitiveType = nvrhi::PrimitiveType::PointList;
        break;
      case Engine::ImportedPrimitiveType::Triangles:
      default:
        break;
      }
      auto gpuMesh = m_RenderDevice.CreateMesh(primitive.vertices, primitive.indices, error, primitiveType);
      if (!gpuMesh) {
        break;
      }
      staged.push_back(
          {.CacheKey = key, .Resource = gpuMesh, .Vertices = primitive.vertices, .Indices = primitive.indices});
      meshes.at(meshIndex).push_back(std::move(gpuMesh));
    }
    if (!error.empty()) {
      break;
    }
  }
  if (!error.empty()) {
    for (auto& mesh : staged) {
      Retire(std::move(mesh.Resource));
    }
    meshes.clear();
    return false;
  }

  for (auto it = m_Meshes.begin(); it != m_Meshes.end();) {
    if ((it->first.Guid == guid || it->first.Guid.starts_with(guid + "/")) && it->first.Generation != generation) {
      Retire(std::move(it->second));
      it = m_Meshes.erase(it);
    } else {
      ++it;
    }
  }
  for (auto& mesh : staged) {
    QueueMeshUpload(mesh.Resource, std::move(mesh.Vertices), std::move(mesh.Indices));
    m_Meshes.emplace(std::move(mesh.CacheKey), std::move(mesh.Resource));
  }
  return true;
}

void ResourceManager::Release(const std::string& guid) {
  size_t retiredMeshes = 0;
  size_t retiredTextures = 0;
  for (auto it = m_Meshes.begin(); it != m_Meshes.end();) {
    if (it->first.Guid == guid || it->first.Guid.starts_with(guid + "/")) {
      Retire(std::move(it->second));
      it = m_Meshes.erase(it);
      ++retiredMeshes;
    } else {
      ++it;
    }
  }
  for (auto it = m_Textures.begin(); it != m_Textures.end();) {
    if (it->first.Guid == guid || it->first.Guid.starts_with(guid + "/")) {
      Retire(std::move(it->second));
      it = m_Textures.erase(it);
      ++retiredTextures;
    } else {
      ++it;
    }
  }
  if (retiredMeshes != 0 || retiredTextures != 0) {
    ENGINE_LOG_DEBUG("Asset", "GPU resources evicted | guid: {} | mesh_count: {} | texture_count: {}", guid,
                     retiredMeshes, retiredTextures);
  }
}

void ResourceManager::Retire(std::shared_ptr<Mesh> mesh) {
  if (mesh) {
    m_RetiredMeshes.push_back({.RetireAfterFrame = m_SubmittedFrames + FramesInFlight, .Resource = std::move(mesh)});
  }
}

void ResourceManager::Retire(std::shared_ptr<TextureResource> texture) {
  if (texture) {
    m_RetiredTextures.push_back(
        {.RetireAfterFrame = m_SubmittedFrames + FramesInFlight, .Resource = std::move(texture)});
  }
}

void ResourceManager::AdvanceSubmittedFrame(uint64_t submittedFrame, uint64_t completedFrame) {
  m_SubmittedFrames = (std::max)(m_SubmittedFrames, submittedFrame);
  m_CompletedFrames = (std::max)(m_CompletedFrames, completedFrame);
  RetireCompletedUploads();

  while (!m_RetiredMeshes.empty() && m_RetiredMeshes.front().RetireAfterFrame <= m_CompletedFrames) {
    m_RetiredMeshes.pop_front();
  }
  while (!m_RetiredTextures.empty() && m_RetiredTextures.front().RetireAfterFrame <= m_CompletedFrames) {
    m_RetiredTextures.pop_front();
  }
}

void ResourceManager::QueueMeshUpload(const std::shared_ptr<Mesh>& mesh, std::vector<Vertex> vertices,
                                      std::vector<uint32_t> indices) {
  if (!mesh) {
    return;
  }
  const uint64_t bytes = (static_cast<uint64_t>(vertices.size()) * sizeof(Vertex)) +
                         (static_cast<uint64_t>(indices.size()) * sizeof(uint32_t));
  m_PendingUploads.push_back({
      .Record = [mesh, vertices = std::move(vertices), indices = std::move(indices)](nvrhi::ICommandList* commandList)
          -> void { MeshBackendAccess::UploadData(*mesh, commandList, vertices, indices); },
      .Bytes = bytes,
  });
  m_UploadProgress.BytesQueued += bytes;
}

void ResourceManager::QueueTextureUpload(const std::shared_ptr<TextureResource>& texture,
                                         const Engine::ImportedImage& image, bool srgb, bool normalMap,
                                         bool preserveAlphaCoverage, float alphaCutoff) {
  if (!texture || (TextureBackendAccess::GetTexture(*texture) == nullptr)) {
    return;
  }
  std::vector<TextureMipGenerator::Mip> mips = TextureMipGenerator::Build(
      image, {.Srgb = srgb, .NormalMap = normalMap, .AlphaCutoff = preserveAlphaCoverage ? alphaCutoff : -1.0F});
  uint64_t bytes = 0;
  for (const auto& mip : mips) {
    bytes += mip.Pixels.size();
  }
  m_PendingUploads.push_back({
      .Record = [texture, mips = std::move(mips)](nvrhi::ICommandList* commandList) -> void {
        for (size_t i = 0; i < mips.size(); ++i) {
          commandList->writeTexture(TextureBackendAccess::GetTexture(*texture), 0, static_cast<uint32_t>(i),
                                    mips.at(i).Pixels.data(), static_cast<size_t>(mips.at(i).Width) * 4, 0);
        }
      },
      .Bytes = bytes,
  });
  m_UploadProgress.BytesQueued += bytes;
}

auto ResourceManager::FlushUploads(std::string& error) -> bool {
  error.clear();
  RetireCompletedUploads();
  if (m_PendingUploads.empty()) {
    return true;
  }
  if (m_Device == nullptr) {
    error = "upload batch requires a render device";
  } else {
    nvrhi::CommandListParameters parameters;
    parameters.setEnableImmediateExecution(true);
    nvrhi::CommandListHandle commandList = m_Device->createCommandList(parameters);
    if (commandList == nullptr) {
      error = "NVRHI failed to create an upload batch command list";
    } else {
      commandList->open();
      for (const PendingUpload& upload : m_PendingUploads) {
        upload.Record(commandList);
      }
      commandList->close();
      m_Device->executeCommandList(commandList);
      nvrhi::EventQueryHandle query = m_Device->createEventQuery();
      if (query != nullptr) {
        m_Device->setEventQuery(query, nvrhi::CommandQueue::Graphics);
        InFlightUploadBatch batch;
        batch.CompletionQuery = std::move(query);
        batch.Staging = std::move(m_PendingUploads);
        m_InFlightUploads.push_back(std::move(batch));
      } else {
        m_Device->waitForIdle();
        m_UploadProgress.BytesCompleted = m_UploadProgress.BytesQueued;
        m_PendingUploads.clear();
        error = "GPU upload completion query allocation failed; staging data was retired after an "
                "idle wait";
      }
      m_UploadProgress.BytesSubmitted = m_UploadProgress.BytesQueued;
      ++m_UploadProgress.BatchesSubmitted;
    }
  }
  if (!error.empty()) {
    ++m_UploadProgress.FailedBatches;
    m_UploadProgress.LastError = error;
    return false;
  }
  return true;
}

void ResourceManager::RetireCompletedUploads() {
  if (m_Device == nullptr) {
    return;
  }
  while (!m_InFlightUploads.empty() && m_Device->pollEventQuery(m_InFlightUploads.front().CompletionQuery)) {
    InFlightUploadBatch& batch = m_InFlightUploads.front();
    for (const PendingUpload& upload : batch.Staging) {
      m_UploadProgress.BytesCompleted += upload.Bytes;
    }
    m_Device->resetEventQuery(batch.CompletionQuery);
    m_InFlightUploads.pop_front();
  }
}
} // namespace Engine::Renderer
