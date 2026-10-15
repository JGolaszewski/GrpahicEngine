#pragma once

#include "rendering/include/renderer/Vertex.h"
#include "runtime/include/CoreExport.h"

#include <DirectXMath.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {
/// Primitive topology understood by the asset importer.
enum class ImportedPrimitiveType { Triangles, Lines, LineStrip, Points };

/// One drawable primitive imported from a model mesh.
struct ImportedPrimitive {
  std::string name;                                 ///< Source primitive name, when provided.
  std::vector<::Engine::Renderer::Vertex> vertices; ///< Expanded vertex attributes for this primitive.
  std::vector<uint32_t> indices;                    ///< Index sequence used to assemble vertices into faces or lines.
  ImportedPrimitiveType primitiveType = ImportedPrimitiveType::Triangles; ///< Topology of the index sequence.
  int32_t materialIndex = -1;       ///< Index into ImportedModel::materials, or -1 when unassigned.
  DirectX::XMFLOAT3 boundsMin = {}; ///< Minimum local-space bound of the vertex positions.
  DirectX::XMFLOAT3 boundsMax = {}; ///< Maximum local-space bound of the vertex positions.
};

/// Mesh container holding the primitives referenced by imported nodes.
struct ImportedMesh {
  std::string name;                          ///< Source mesh name, when provided.
  std::vector<ImportedPrimitive> primitives; ///< Drawable parts and their material assignments.
};

/// Scene-graph node describing a mesh instance and its local transform.
struct ImportedNode {
  std::string name;                   ///< Source node name, when provided.
  int32_t parentIndex = -1;           ///< Parent index in ImportedModel::nodes, or -1 for a root node.
  int32_t meshIndex = -1;             ///< Referenced mesh index, or -1 when the node has no mesh.
  std::vector<uint32_t> children;     ///< Child node indices in ImportedModel::nodes.
  DirectX::XMFLOAT3 translation = {}; ///< Local translation used when no matrix is supplied.
  DirectX::XMFLOAT4 rotation = {0.0f, 0.0f, 0.0f, 1.0f}; ///< Local quaternion rotation.
  DirectX::XMFLOAT3 scale = {1.0f, 1.0f, 1.0f};          ///< Local scale used when no matrix is supplied.
  DirectX::XMFLOAT4X4 matrix = {};                       ///< Source local transform matrix when hasMatrix is true.
  bool hasMatrix = false;                                ///< Whether matrix contains the node's authored transform.
};

/// Decoded image bytes in tightly packed RGBA8 format.
struct ImportedImage {
  std::string name;           ///< Source image name, when provided.
  std::string mimeType;       ///< Declared source MIME type.
  uint32_t width = 0;         ///< Image width in pixels.
  uint32_t height = 0;        ///< Image height in pixels.
  std::vector<uint8_t> rgba8; ///< Pixel data with four bytes per pixel.
};

/// UV transform applied when sampling a material texture.
struct ImportedTextureTransform {
  DirectX::XMFLOAT2 offset = {0.0f, 0.0f}; ///< UV offset in texture-coordinate space.
  DirectX::XMFLOAT2 scale = {1.0f, 1.0f};  ///< Per-axis UV scale.
  float rotation = 0.0f;                   ///< UV rotation in radians.
};

/// Material factors, texture references, and sampling metadata imported from a model.
struct ImportedMaterial {
  std::string name;                                       ///< Source material name, when provided.
  bool specularGlossiness = false;                        ///< Selects the specular-glossiness workflow when true.
  DirectX::XMFLOAT4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Base color factor including alpha.
  DirectX::XMFLOAT3 emissive = {};                        ///< Emissive RGB factor.
  DirectX::XMFLOAT3 specular = {1.0f, 1.0f, 1.0f};        ///< Specular RGB factor for the glossiness workflow.
  float metallic = 1.0f;                                  ///< Metallic factor in the metallic-roughness workflow.
  float roughness = 1.0f;                                 ///< Roughness factor in the metallic-roughness workflow.
  float glossiness = 1.0f;                                ///< Glossiness factor in the specular-glossiness workflow.
  float alphaCutoff = 0.5f;                               ///< Alpha threshold used by masked materials.
  float normalScale = 1.0f;                               ///< Scale applied to sampled normal-map XY values.
  float occlusionStrength = 1.0f;                         ///< Blend strength for sampled occlusion.
  int32_t baseColorTexture = -1;                          ///< Texture index for base color, or -1 when absent.
  int32_t metallicRoughnessTexture = -1;                  ///< Texture index for metallic-roughness, or -1 when absent.
  int32_t normalTexture = -1;                             ///< Texture index for the normal map, or -1 when absent.
  int32_t occlusionTexture = -1;                          ///< Texture index for occlusion, or -1 when absent.
  int32_t emissiveTexture = -1;                           ///< Texture index for emission, or -1 when absent.
  uint32_t baseColorTexCoord = 0;                         ///< UV set used by the base-color texture.
  uint32_t metallicRoughnessTexCoord = 0;                 ///< UV set used by the metallic-roughness texture.
  uint32_t normalTexCoord = 0;                            ///< UV set used by the normal texture.
  uint32_t occlusionTexCoord = 0;                         ///< UV set used by the occlusion texture.
  uint32_t emissiveTexCoord = 0;                          ///< UV set used by the emissive texture.
  ImportedTextureTransform baseColorTransform;            ///< UV transform for base color.
  ImportedTextureTransform metallicRoughnessTransform;    ///< UV transform for metallic-roughness.
  ImportedTextureTransform normalTransform;               ///< UV transform for the normal map.
  ImportedTextureTransform occlusionTransform;            ///< UV transform for occlusion.
  ImportedTextureTransform emissiveTransform;             ///< UV transform for emission.
  std::string alphaMode = "OPAQUE";                       ///< Source alpha mode: OPAQUE, MASK, or BLEND.
  bool doubleSided = false;                               ///< Whether back-face culling should be disabled.
};

/// Sampler and image indices associated with a material texture.
struct ImportedTexture {
  int32_t imageIndex = -1;   ///< Referenced ImportedModel::images entry.
  int32_t samplerIndex = -1; ///< Source sampler index, or -1 when unspecified.
  int32_t magFilter = -1;    ///< Source magnification filter enum, or -1 for the default.
  int32_t minFilter = -1;    ///< Source minification filter enum, or -1 for the default.
  int32_t wrapS = 10497;     ///< Horizontal address mode using glTF sampler values.
  int32_t wrapT = 10497;     ///< Vertical address mode using glTF sampler values.
};

/// Complete CPU-side representation of an imported model and its source scene graph.
struct ImportedModel {
  std::filesystem::path sourcePath;          ///< Path from which the model was imported.
  std::vector<std::string> importWarnings;   ///< Non-fatal issues encountered during import.
  int32_t defaultScene = -1;                 ///< Default entry in scenes, or -1 when no default is declared.
  std::vector<std::vector<uint32_t>> scenes; ///< Root-node indices for each source scene.
  std::vector<ImportedNode> nodes;           ///< Imported scene-graph nodes.
  std::vector<ImportedMesh> meshes;          ///< Imported mesh and primitive data.
  std::vector<ImportedMaterial> materials;   ///< Imported material data.
  std::vector<ImportedTexture> textures;     ///< Imported texture sampler/image references.
  std::vector<ImportedImage> images;         ///< Decoded image pixel data.
};

/// Result of importing a model; an empty error string indicates success.
struct AssetImportResult {
  ImportedModel model;               ///< Imported scene, geometry, materials, and images.
  std::vector<std::string> warnings; ///< Recoverable import issues.
  std::string error;                 ///< Fatal import error, or empty on success.

  /// Reports whether importing completed without a fatal error.
  explicit operator bool() const noexcept { return error.empty(); }
};

/// Result of importing a standalone image; an empty error string indicates success.
struct ImageImportResult {
  ImportedImage image; ///< Decoded dimensions, metadata, and RGBA8 pixels.
  std::string error;   ///< Fatal image import error, or empty on success.

  /// Reports whether decoding completed without a fatal error.
  explicit operator bool() const noexcept { return error.empty(); }
};

/// Decodes supported model and image formats into engine-owned CPU data.
class ENGINE_API AssetImporter {
public:
  /// Imports a glTF or GLB file and all referenced CPU-side model data.
  /// @param sourcePath Path to the source model file.
  /// @return Imported model data and warnings, or an error when import fails.
  AssetImportResult ImportGltf(const std::filesystem::path& sourcePath) const;

  /// Decodes a standalone image into tightly packed RGBA8 pixels.
  /// @param sourcePath Path to the source image file.
  /// @return Decoded image data, or an error when decoding fails.
  ImageImportResult ImportImage(const std::filesystem::path& sourcePath) const;
};
} // namespace Engine
