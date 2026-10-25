#pragma once

#include "runtime/include/CoreExport.h"
#include "scene/include/components/CameraPostProcessing.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Engine {
inline constexpr uint32_t CurrentSceneSchemaVersion = 4;

/// Persistent 128-bit scene identity stored as two unsigned words.
struct SceneGuidDocument {
  uint64_t high = 0; ///< Most-significant 64 bits of the identity.
  uint64_t low = 0;  ///< Least-significant 64 bits of the identity.
  /// Compares both words of the persistent identity.
  /// @param other Identity to compare with.
  /// @return True when both identity words match.
  auto operator==(const SceneGuidDocument& other) const noexcept -> bool = default;
};

/// Material properties serialized with a scene entity.
struct SceneMaterialTextureTransform {
  std::array<float, 2> offset{};
  std::array<float, 2> scale = {1.0f, 1.0f};
  float rotation = 0.0f;
  auto operator==(const SceneMaterialTextureTransform& other) const noexcept -> bool = default;
};

struct SceneMaterialDocument {
  std::array<float, 4> baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Linear RGBA base color.
  std::array<float, 3> emissive{};                           ///< Linear RGB emissive contribution.
  float metallic = 0.0f;                                     ///< Metallic workflow factor in [0, 1].
  float roughness = 0.5f;                                    ///< Surface roughness factor in [0, 1].
  float gtrGamma = 2.0f;                                     ///< GTR distribution parameter used by the selected BRDF.
  float anisotropicRoughnessU = 0.5f;                        ///< Ashikhmin-Shirley tangent-axis roughness.
  float anisotropicRoughnessV = 0.5f;                        ///< Ashikhmin-Shirley bitangent-axis roughness.
  float ior = 1.5f; ///< Index of refraction used to derive dielectric reflectance.
  std::array<float, 3> specularF0 = {0.04f, 0.04f, 0.04f}; ///< RGB reflectance at normal incidence.
  std::string brdfModel = "ggx"; ///< BRDF identifier, including stable "blinnPhong", "schlick", and "sawicki" tokens.
  std::string workflow = "metallicRoughness";      ///< Material parameter workflow identifier.
  std::string specularMode = "ior";                ///< Source mode used to derive specular reflectance.
  std::string alphaMode = "opaque";                ///< Alpha handling mode for the material.
  float alphaCutoff = 0.5f;                        ///< Mask threshold used when alpha mode is masked.
  bool doubleSided = false;                        ///< Whether the material renders both face orientations.
  float normalScale = 1.0f;                        ///< Strength applied to the tangent-space normal map.
  std::optional<std::string> baseColorTextureGuid; ///< Optional base-color texture identity.
  std::optional<std::string> normalTextureGuid;    ///< Optional tangent-space normal texture identity.
  uint32_t baseColorTexCoord = 0;
  uint32_t normalTexCoord = 0;
  SceneMaterialTextureTransform baseColorTextureTransform;
  SceneMaterialTextureTransform normalTextureTransform;
  std::array<uint32_t, 5> textureAnisotropyLevels{}; ///< Per-slot requests: 0 means Global cap, otherwise 1/2/4/8/16.
  /// Compares every serialized material property.
  /// @param other Material document to compare with.
  /// @return True when all properties match.
  auto operator==(const SceneMaterialDocument& other) const noexcept -> bool = default;
};

/// Persistent entity state stored in a scene document.
struct SceneEntityDocument {
  uint64_t id = 0;        ///< Document-local numeric entity identifier.
  std::string name;       ///< Human-readable entity name.
  bool sceneRoot = false; ///< Marks the structural root; its authored transform/visibility are omitted.
  std::optional<SceneGuidDocument> identityGuid; ///< Optional persistent identity.
  std::optional<SceneGuidDocument> parentGuid;   ///< Optional persistent identity of the parent entity.
  bool visible = true; ///< Whether the entity is visible in scene views; ignored for the structural root.
  std::optional<std::string> modelGuid;     ///< Optional imported model identity.
  std::optional<std::string> materialGuid;  ///< Optional shared material identity.
  std::optional<std::string> primitiveType; ///< Optional built-in primitive shape name.
  bool hasMeshRenderer = false; ///< Whether this entity has a mesh renderer, even without a source assigned.
  std::optional<SceneMaterialDocument> material;                       ///< Optional entity-owned material data.
  std::vector<std::optional<SceneMaterialDocument>> materialOverrides; ///< Per-submesh material overrides.
  std::array<float, 3> position{}; ///< Local translation in scene units; ignored for the structural root.
  std::array<float, 4> rotation = {0.0f, 0.0f, 0.0f, 1.0f}; ///< Local XYZW rotation; ignored for the structural root.
  std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};          ///< Local scale; ignored for the structural root.
  bool flipHorizontal = false;                              ///< Mirrors local geometry across X.
  bool flipVertical = false;                                ///< Mirrors local geometry across Y.
  bool forceWireframe = false;                              ///< Whether this entity is drawn in wireframe.
  bool meshVisible = true;                                  ///< Whether its mesh renderer is submitted.
  bool disableDepthTest = false;                            ///< Whether depth testing is disabled for this entity.
  bool castShadows = true;                                  ///< Whether this entity contributes to shadow maps.
  bool receiveShadows = true;                               ///< Whether this entity receives shadows.
};

/// Authored light state stored in a scene document.
struct SceneLightDocument {
  uint64_t id = 0;                                          ///< Document-local numeric light identifier.
  std::string name = "Spot Light";                          ///< User-facing light entity name.
  std::optional<SceneGuidDocument> identityGuid;            ///< Optional persistent identity.
  std::optional<SceneGuidDocument> parentGuid;              ///< Optional persistent identity of the parent entity.
  bool visible = true;                                      ///< Whether the light is enabled in scene views.
  std::array<float, 3> direction = {0.0f, -1.0f, 0.0f};     ///< World-space direction for directional lights.
  std::array<float, 3> position = {0.0f, 3.0f, 0.0f};       ///< Local position for positional lights.
  std::array<float, 4> rotation = {0.0f, 0.0f, 0.0f, 1.0f}; ///< Local entity rotation as an XYZW quaternion.
  std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};          ///< Local entity scale.
  bool flipHorizontal = false;                              ///< Mirrors local geometry across X.
  bool flipVertical = false;                                ///< Mirrors local geometry across Y.
  std::array<float, 3> color = {1.0f, 1.0f, 1.0f};          ///< Linear RGB light color.
  float intensity = 1.0f;                                   ///< Radiometric intensity multiplier.
  float range = 20.0f;                                      ///< Maximum influence distance for positional lights.
  float innerAngleDegrees = 15.0f;                          ///< Inner spotlight cone angle in degrees.
  float outerAngleDegrees = 25.0f;                          ///< Outer spotlight cone angle in degrees.
  float shadowBias = 0.0015f;                               ///< Constant shadow-map depth bias.
  float shadowNormalBias = 0.0015f;                         ///< Slope-scaled shadow-map bias.
  bool usePosition = false;         ///< Whether the light uses positional rather than directional behavior.
  std::string type = "directional"; ///< Authored light type: directional, point, or spot.
  bool castShadows = true;          ///< Whether the light produces shadows.
};

/// Authored camera state stored in a scene document.
struct SceneCameraDocument {
  uint64_t id = 0;                                          ///< Document-local numeric camera identifier.
  std::string name = "Camera";                              ///< User-facing camera entity name.
  std::optional<SceneGuidDocument> identityGuid;            ///< Optional persistent identity.
  std::optional<SceneGuidDocument> parentGuid;              ///< Optional persistent identity of the parent entity.
  bool visible = true;                                      ///< Whether the camera is available for selection.
  std::array<float, 3> position{};                          ///< Local camera position.
  std::array<float, 4> rotation = {0.0f, 0.0f, 0.0f, 1.0f}; ///< Local rotation as an XYZW quaternion.
  std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};          ///< Local entity scale.
  bool flipHorizontal = false;                              ///< Mirrors local geometry across X.
  bool flipVertical = false;                                ///< Mirrors local geometry across Y.
  float fovRadians = 0.785398163f;                          ///< Vertical field of view in radians.
  float nearPlane = 0.1f;                                   ///< Near clipping plane distance.
  float farPlane = 1000.0f;                                 ///< Far clipping plane distance.
  /// Render output dimensions; missing values in older files use 1920 x 1080.
  uint32_t outputWidth = 1920;
  uint32_t outputHeight = 1080;                            ///< Render output height in pixels.
  int32_t priority = 0;                                    ///< Camera selection priority.
  bool active = true;                                      ///< Whether this camera participates in camera selection.
  std::optional<Ecs::CameraPostProcessing> postProcessing; ///< Optional ordered camera-only effect stack.
};

/// Complete persistent scene state, including its schema, permanent root, and authored objects.
struct SceneDocument {
  uint32_t schemaVersion = CurrentSceneSchemaVersion; ///< Serialized schema version.
  float unitsPerMeter = 1.0f;                         ///< Number of scene units representing one meter.
  std::vector<SceneEntityDocument> entities;          ///< Authored renderable entities.
  std::vector<SceneLightDocument> lights;             ///< Authored lights.
  std::vector<SceneCameraDocument> cameras;           ///< Authored cameras.
};

/// Result of loading or saving a scene document.
struct SceneDocumentResult {
  SceneDocument document;            ///< Loaded document, populated on successful load.
  std::vector<std::string> warnings; ///< Non-fatal issues encountered while loading.
  std::string error;                 ///< Failure description, empty when the operation succeeds.

  /// Reports whether the operation completed without an error.
  explicit operator bool() const noexcept { return error.empty(); }
};

/// Loads and atomically saves serialized scene documents.
class ENGINE_API SceneSerializer {
public:
  /// Loads and validates a scene document from a file.
  /// @param path File to read.
  /// @return The parsed document and any non-fatal warnings, or an error.
  static SceneDocumentResult Load(const std::filesystem::path& path);

  /// Saves a scene document using a temporary file and atomic replacement.
  /// @param path Destination file.
  /// @param document Document to serialize.
  /// @param error Receives a failure description on error and is cleared on success.
  /// @return True when the destination was replaced successfully.
  static bool SaveAtomic(const std::filesystem::path& path, const SceneDocument& document, std::string& error);
};
} // namespace Engine
