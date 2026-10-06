#include "renderer/ResourceManager.h"

#include <algorithm>
#include <functional>

namespace Renderer
{
ResourceManager::ResourceManager(nvrhi::IDevice *device) : m_Device(device), m_RenderDevice(device)
{
    Engine::ImportedImage color;
    color.name = "FallbackBaseColor";
    color.width = color.height = 1;
    color.rgba8 = {255, 255, 255, 255};
    std::string error;
    m_FallbackColor = m_RenderDevice.CreateTexture(color, true, error);
    if (m_FallbackColor)
        QueueTextureUpload(m_FallbackColor, color);

    Engine::ImportedImage normal;
    normal.name = "FallbackNormal";
    normal.width = normal.height = 1;
    normal.rgba8 = {128, 128, 255, 255};
    m_FallbackNormal = m_RenderDevice.CreateTexture(normal, false, error);
    if (m_FallbackNormal)
        QueueTextureUpload(m_FallbackNormal, normal);
}

ResourceManager::~ResourceManager()
{
    if (m_Device)
        m_Device->waitForIdle();
    m_PendingUploads.clear();
    m_InFlightUploads.clear();
    m_Meshes.clear();
    m_Textures.clear();
    m_RetiredMeshes.clear();
    m_RetiredTextures.clear();
    m_FallbackColor.reset();
    m_FallbackNormal.reset();
}

size_t ResourceManager::KeyHash::operator()(const Key &key) const noexcept
{
    const size_t guidHash = std::hash<std::string>{}(key.Guid);
    const size_t generationHash = std::hash<uint64_t>{}(key.Generation);
    const size_t combined = guidHash ^ (generationHash + 0x9e3779b9 + (guidHash << 6) + (guidHash >> 2));
    return combined ^ (key.Srgb ? 0x85ebca6b : 0u);
}

std::shared_ptr<Mesh> ResourceManager::GetOrCreateMesh(const std::string &guid, uint64_t generation,
                                                       const std::vector<Vertex> &vertices,
                                                       const std::vector<uint32_t> &indices,
                                                       std::string &error)
{
    error.clear();
    if (guid.empty())
    {
        error = "mesh cache requires a non-empty asset GUID";
        return nullptr;
    }
    const Key key{guid, generation};
    if (const auto it = m_Meshes.find(key); it != m_Meshes.end())
        return it->second;

    auto mesh = m_RenderDevice.CreateMesh(vertices, indices, error);
    if (!mesh)
        return nullptr;
    QueueMeshUpload(mesh, vertices, indices);
    for (auto it = m_Meshes.begin(); it != m_Meshes.end();)
    {
        if (it->first.Guid == guid || it->first.Guid.starts_with(guid + "/"))
        {
            Retire(std::move(it->second));
            it = m_Meshes.erase(it);
        }
        else
        {
            ++it;
        }
    }
    m_Meshes.emplace(key, mesh);
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::GetMesh(const std::string &guid, uint64_t generation) const
{
    const auto it = m_Meshes.find(Key{guid, generation, false});
    return it == m_Meshes.end() ? nullptr : it->second;
}

std::shared_ptr<TextureResource> ResourceManager::GetOrCreateTexture(const std::string &identity,
                                                                     uint64_t generation,
                                                                     const Engine::ImportedImage &image,
                                                                     bool srgb, std::string &error)
{
    error.clear();
    if (identity.empty())
    {
        error = "texture cache requires a non-empty asset or subresource identity";
        return nullptr;
    }
    const Key key{identity, generation, srgb};
    if (const auto it = m_Textures.find(key); it != m_Textures.end())
        return it->second;
    auto texture = m_RenderDevice.CreateTexture(image, srgb, error);
    if (!texture)
        return nullptr;
    QueueTextureUpload(texture, image);
    for (auto it = m_Textures.begin(); it != m_Textures.end();)
    {
        if ((it->first.Guid == identity || it->first.Guid.starts_with(identity + "/")) &&
            it->first.Generation != generation)
        {
            Retire(std::move(it->second));
            it = m_Textures.erase(it);
        }
        else
        {
            ++it;
        }
    }
    m_Textures.emplace(key, texture);
    return texture;
}

std::shared_ptr<TextureResource> ResourceManager::GetFallbackTexture(bool srgb) const
{
    return srgb ? m_FallbackColor : m_FallbackNormal;
}

bool ResourceManager::GetOrCreateModelMeshes(const std::string &guid, uint64_t generation,
                                             const Engine::ImportedModel &model,
                                             std::vector<std::vector<std::shared_ptr<Mesh>>> &meshes,
                                             std::string &error)
{
    error.clear();
    meshes.clear();
    if (guid.empty() || model.meshes.empty())
    {
        error = "model upload requires an asset GUID and at least one mesh";
        return false;
    }

    struct StagedMesh
    {
        Key CacheKey;
        std::shared_ptr<Mesh> Resource;
        std::vector<Vertex> Vertices;
        std::vector<uint32_t> Indices;
    };
    std::vector<StagedMesh> staged;
    meshes.resize(model.meshes.size());
    for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
    {
        const auto &sourceMesh = model.meshes[meshIndex];
        if (sourceMesh.primitives.empty())
        {
            error = "model mesh " + std::to_string(meshIndex) + " contains no primitives";
            break;
        }
        meshes[meshIndex].reserve(sourceMesh.primitives.size());
        for (size_t primitiveIndex = 0; primitiveIndex < sourceMesh.primitives.size(); ++primitiveIndex)
        {
            const std::string subresource =
                "mesh/" + std::to_string(meshIndex) + "/primitive/" + std::to_string(primitiveIndex);
            const Key key{guid + "/" + subresource, generation};
            if (const auto cached = m_Meshes.find(key); cached != m_Meshes.end())
            {
                meshes[meshIndex].push_back(cached->second);
                continue;
            }
            const auto &primitive = sourceMesh.primitives[primitiveIndex];
            auto gpuMesh = m_RenderDevice.CreateMesh(primitive.vertices, primitive.indices, error);
            if (!gpuMesh)
                break;
            staged.push_back({key, gpuMesh, primitive.vertices, primitive.indices});
            meshes[meshIndex].push_back(std::move(gpuMesh));
        }
        if (!error.empty())
            break;
    }
    if (!error.empty())
    {
        for (auto &mesh : staged)
        {
            Retire(std::move(mesh.Resource));
        }
        meshes.clear();
        return false;
    }

    for (auto it = m_Meshes.begin(); it != m_Meshes.end();)
    {
        if ((it->first.Guid == guid || it->first.Guid.starts_with(guid + "/")) &&
            it->first.Generation != generation)
        {
            Retire(std::move(it->second));
            it = m_Meshes.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (auto &mesh : staged)
    {
        QueueMeshUpload(mesh.Resource, std::move(mesh.Vertices), std::move(mesh.Indices));
        m_Meshes.emplace(std::move(mesh.CacheKey), std::move(mesh.Resource));
    }
    return true;
}

void ResourceManager::Release(const std::string &guid)
{
    for (auto it = m_Meshes.begin(); it != m_Meshes.end();)
    {
        if (it->first.Guid == guid || it->first.Guid.starts_with(guid + "/"))
        {
            Retire(std::move(it->second));
            it = m_Meshes.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (auto it = m_Textures.begin(); it != m_Textures.end();)
    {
        if (it->first.Guid == guid || it->first.Guid.starts_with(guid + "/"))
        {
            Retire(std::move(it->second));
            it = m_Textures.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ResourceManager::Retire(std::shared_ptr<Mesh> mesh)
{
    if (mesh)
        m_RetiredMeshes.push_back({m_SubmittedFrames + FramesInFlight, std::move(mesh)});
}

void ResourceManager::Retire(std::shared_ptr<TextureResource> texture)
{
    if (texture)
        m_RetiredTextures.push_back({m_SubmittedFrames + FramesInFlight, std::move(texture)});
}

void ResourceManager::AdvanceSubmittedFrame(uint64_t submittedFrame, uint64_t completedFrame)
{
    m_SubmittedFrames = (std::max)(m_SubmittedFrames, submittedFrame);
    m_CompletedFrames = (std::max)(m_CompletedFrames, completedFrame);
    RetireCompletedUploads();

    while (!m_RetiredMeshes.empty() && m_RetiredMeshes.front().RetireAfterFrame <= m_CompletedFrames)
    {
        m_RetiredMeshes.pop_front();
    }
    while (!m_RetiredTextures.empty() && m_RetiredTextures.front().RetireAfterFrame <= m_CompletedFrames)
    {
        m_RetiredTextures.pop_front();
    }
}

void ResourceManager::QueueMeshUpload(const std::shared_ptr<Mesh> &mesh, std::vector<Vertex> vertices,
                                      std::vector<uint32_t> indices)
{
    if (!mesh)
        return;
    const uint64_t bytes = static_cast<uint64_t>(vertices.size()) * sizeof(Vertex) +
                           static_cast<uint64_t>(indices.size()) * sizeof(uint32_t);
    m_PendingUploads.push_back(
        {[mesh, vertices = std::move(vertices), indices = std::move(indices)](
             nvrhi::ICommandList *commandList) { mesh->UploadData(commandList, vertices, indices); },
         bytes});
    m_UploadProgress.BytesQueued += bytes;
}

void ResourceManager::QueueTextureUpload(const std::shared_ptr<TextureResource> &texture,
                                         const Engine::ImportedImage &image)
{
    if (!texture || !texture->Texture)
        return;
    const uint32_t width = image.width;
    std::vector<uint8_t> pixels = image.rgba8;
    const uint64_t bytes = pixels.size();
    m_PendingUploads.push_back(
        {[texture, width, pixels = std::move(pixels)](nvrhi::ICommandList *commandList) {
             commandList->writeTexture(texture->Texture, 0, 0, pixels.data(), static_cast<size_t>(width) * 4,
                                       0);
         },
         bytes});
    m_UploadProgress.BytesQueued += bytes;
}

bool ResourceManager::FlushUploads(std::string &error)
{
    error.clear();
    RetireCompletedUploads();
    if (m_PendingUploads.empty())
        return true;
    if (!m_Device)
    {
        error = "upload batch requires a render device";
    }
    else
    {
        nvrhi::CommandListParameters parameters;
        parameters.setEnableImmediateExecution(false);
        nvrhi::CommandListHandle commandList = m_Device->createCommandList(parameters);
        if (!commandList)
        {
            error = "NVRHI failed to create an upload batch command list";
        }
        else
        {
            commandList->open();
            for (const PendingUpload &upload : m_PendingUploads)
                upload.Record(commandList);
            commandList->close();
            m_Device->executeCommandList(commandList);
            nvrhi::EventQueryHandle query = m_Device->createEventQuery();
            if (query)
            {
                m_Device->setEventQuery(query, nvrhi::CommandQueue::Graphics);
                InFlightUploadBatch batch;
                batch.CompletionQuery = std::move(query);
                batch.Staging = std::move(m_PendingUploads);
                m_InFlightUploads.push_back(std::move(batch));
            }
            else
            {
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
    if (!error.empty())
    {
        ++m_UploadProgress.FailedBatches;
        m_UploadProgress.LastError = error;
        return false;
    }
    return true;
}

void ResourceManager::RetireCompletedUploads()
{
    if (!m_Device)
        return;
    while (!m_InFlightUploads.empty() && m_Device->pollEventQuery(m_InFlightUploads.front().CompletionQuery))
    {
        InFlightUploadBatch &batch = m_InFlightUploads.front();
        for (const PendingUpload &upload : batch.Staging)
            m_UploadProgress.BytesCompleted += upload.Bytes;
        m_Device->resetEventQuery(batch.CompletionQuery);
        m_InFlightUploads.pop_front();
    }
}
} // namespace Renderer
