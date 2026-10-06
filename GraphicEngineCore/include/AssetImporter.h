#pragma once

#include "CoreExport.h"
#include "renderer/Mesh.h"

#include <DirectXMath.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine
{
struct ImportedPrimitive
{
    std::string name;
    std::vector<Renderer::Vertex> vertices;
    std::vector<uint32_t> indices;
    nvrhi::PrimitiveType primitiveType = nvrhi::PrimitiveType::TriangleList;
    int32_t materialIndex = -1;
    DirectX::XMFLOAT3 boundsMin = {};
    DirectX::XMFLOAT3 boundsMax = {};
};

struct ImportedMesh
{
    std::string name;
    std::vector<ImportedPrimitive> primitives;
};

struct ImportedNode
{
    std::string name;
    int32_t parentIndex = -1;
    int32_t meshIndex = -1;
    std::vector<uint32_t> children;
    DirectX::XMFLOAT3 translation = {};
    DirectX::XMFLOAT4 rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 scale = {1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT4X4 matrix = {};
    bool hasMatrix = false;
};

struct ImportedImage
{
    std::string name;
    std::string mimeType;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba8;
};

struct ImportedTextureTransform
{
    DirectX::XMFLOAT2 offset = {0.0f, 0.0f};
    DirectX::XMFLOAT2 scale = {1.0f, 1.0f};
    float rotation = 0.0f;
};

struct ImportedMaterial
{
    std::string name;
    DirectX::XMFLOAT4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT3 emissive = {};
    float metallic = 1.0f;
    float roughness = 1.0f;
    float alphaCutoff = 0.5f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    int32_t baseColorTexture = -1;
    int32_t metallicRoughnessTexture = -1;
    int32_t normalTexture = -1;
    int32_t occlusionTexture = -1;
    int32_t emissiveTexture = -1;
    uint32_t baseColorTexCoord = 0;
    uint32_t metallicRoughnessTexCoord = 0;
    uint32_t normalTexCoord = 0;
    uint32_t occlusionTexCoord = 0;
    uint32_t emissiveTexCoord = 0;
    ImportedTextureTransform baseColorTransform;
    ImportedTextureTransform metallicRoughnessTransform;
    ImportedTextureTransform normalTransform;
    ImportedTextureTransform occlusionTransform;
    ImportedTextureTransform emissiveTransform;
    std::string alphaMode = "OPAQUE";
    bool doubleSided = false;
};

struct ImportedTexture
{
    int32_t imageIndex = -1;
    int32_t samplerIndex = -1;
    int32_t magFilter = -1;
    int32_t minFilter = -1;
    int32_t wrapS = 10497;
    int32_t wrapT = 10497;
};

struct ImportedModel
{
    std::filesystem::path sourcePath;
    std::vector<std::string> importWarnings;
    int32_t defaultScene = -1;
    std::vector<std::vector<uint32_t>> scenes;
    std::vector<ImportedNode> nodes;
    std::vector<ImportedMesh> meshes;
    std::vector<ImportedMaterial> materials;
    std::vector<ImportedTexture> textures;
    std::vector<ImportedImage> images;
};

struct AssetImportResult
{
    ImportedModel model;
    std::vector<std::string> warnings;
    std::string error;

    explicit operator bool() const noexcept
    {
        return error.empty();
    }
};

struct ImageImportResult
{
    ImportedImage image;
    std::string error;

    explicit operator bool() const noexcept
    {
        return error.empty();
    }
};

class ENGINE_API AssetImporter
{
  public:
    AssetImportResult ImportGltf(const std::filesystem::path &sourcePath) const;
    ImageImportResult ImportImage(const std::filesystem::path &sourcePath) const;
};
} // namespace Engine
