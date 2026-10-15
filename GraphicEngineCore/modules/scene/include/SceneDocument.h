#pragma once

#include "runtime/include/CoreExport.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Engine
{
/// Current JSON scene-document schema version accepted for writing.
inline constexpr uint32_t CurrentSceneSchemaVersion = 1;

/// Serializable material properties and optional texture asset references.
struct SceneMaterialDocument
{
    std::array<float, 4> baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Linear RGBA base color factor.
    std::array<float, 3> emissive{};                           ///< Linear RGB emission factor.
    float metallic = 0.0f;  ///< Metallic-roughness workflow metalness factor.
    float roughness = 0.5f; ///< Surface roughness factor.
    float gtrGamma = 2.0f;  ///< Gamma parameter used by the GTR BRDF model.
    float ior = 1.5f;       ///< Index of refraction used to derive dielectric F0.
    std::array<float, 3> specularF0 = {0.04f, 0.04f,
                                       0.04f};       ///< Explicit dielectric reflectance at normal incidence.
    std::string brdfModel = "legacy";                ///< Serialized BRDF model name.
    std::string workflow = "metallicRoughness";      ///< Material workflow identifier.
    std::string specularMode = "ior";                ///< Dielectric specular source: IOR or explicit F0.
    std::string alphaMode = "opaque";                ///< Alpha behavior: opaque, mask, or blend.
    float alphaCutoff = 0.5f;                        ///< Alpha threshold used in mask mode.
    bool doubleSided = false;                        ///< Whether both triangle sides are visible.
    std::optional<std::string> baseColorTextureGuid; ///< Optional base-color texture asset identifier.
    std::optional<std::string> normalTextureGuid;    ///< Optional tangent-space normal texture identifier.
};

/// Serializable identity, source, transform, and render flags for one entity.
struct SceneEntityDocument
{
    uint64_t id = 0;                      ///< Stable scene-local entity identifier.
    std::string name;                     ///< User-facing entity name.
    std::optional<std::string> modelGuid; ///< Model asset identifier, when the entity uses an imported model.
    std::optional<std::string> materialGuid;       ///< Reserved standalone material asset identifier.
    std::optional<std::string> primitiveType;      ///< Primitive shape name, when the entity is procedural.
    std::optional<SceneMaterialDocument> material; ///< Material override for a procedural primitive.
    std::vector<std::optional<SceneMaterialDocument>>
        materialOverrides;                                    ///< Per-model-material slot overrides.
    std::array<float, 3> position{};                          ///< Local translation in engine units.
    std::array<float, 4> rotation = {0.0f, 0.0f, 0.0f, 1.0f}; ///< Local quaternion in XYZW order.
    std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};          ///< Local scale on each axis.
    bool forceWireframe = false;   ///< Whether to render this entity in wireframe mode.
    bool disableDepthTest = false; ///< Whether depth testing is disabled for this entity.
    bool castShadows = true;       ///< Whether this entity contributes to shadow maps.
    bool receiveShadows = true;    ///< Whether this entity receives shadowing.
};

/// Serializable spotlight position, direction, photometry, and shadow settings.
struct SceneLightDocument
{
    uint64_t id = 0;                                      ///< Stable scene-local light identifier.
    std::array<float, 3> direction = {0.0f, -1.0f, 0.0f}; ///< Spotlight direction in world space.
    std::array<float, 3> position = {0.0f, 3.0f, 0.0f};   ///< Spotlight position in world space.
    std::array<float, 3> color = {1.0f, 1.0f, 1.0f};      ///< Linear RGB light color.
    float intensity = 1.0f;                               ///< Scalar light intensity.
    float range = 20.0f;                                  ///< Maximum light influence distance.
    float innerAngleDegrees = 15.0f;                      ///< Full-strength cone angle in degrees.
    float outerAngleDegrees = 25.0f;                      ///< Cone cutoff angle in degrees.
    float shadowBias = 0.0015f;                           ///< Constant depth bias for shadow comparison.
    float shadowNormalBias = 0.0015f; ///< Surface-normal offset used for shadow comparison.
    bool usePosition = false;         ///< Whether shading uses the positional-light path.
    bool castShadows = true;          ///< Whether this light produces a shadow map.
};

/// Serializable camera transform and perspective projection values.
struct SceneCameraDocument
{
    uint64_t id = 0;                                          ///< Stable scene-local camera identifier.
    std::array<float, 3> position{};                          ///< Camera position in world space.
    std::array<float, 4> rotation = {0.0f, 0.0f, 0.0f, 1.0f}; ///< Camera quaternion in XYZW order.
    float fovRadians = 0.785398163f;                          ///< Vertical field of view in radians.
    float nearPlane = 0.1f;                                   ///< Near clipping distance; must be positive.
    float farPlane = 1000.0f; ///< Far clipping distance; must exceed nearPlane.
};

/// Complete in-memory representation of one serialized scene file.
struct SceneDocument
{
    uint32_t schemaVersion = CurrentSceneSchemaVersion; ///< Schema version used to encode this document.
    float unitsPerMeter = 1.0f;                ///< Number of engine units corresponding to one meter.
    std::vector<SceneEntityDocument> entities; ///< Entities in stable serialized order.
    std::vector<SceneLightDocument> lights;    ///< Lights in stable serialized order.
    std::vector<SceneCameraDocument> cameras;  ///< Cameras in stable serialized order.
};

/// Result of loading a scene document; an empty error string indicates success.
struct SceneDocumentResult
{
    SceneDocument document;            ///< Parsed and migrated document.
    std::vector<std::string> warnings; ///< Non-fatal compatibility or data warnings.
    std::string error;                 ///< Fatal parse or validation error, or empty on success.

    /// Reports whether loading completed without a fatal error.
    explicit operator bool() const noexcept
    {
        return error.empty();
    }
};

/// Reads, validates, migrates, and atomically writes scene documents.
class ENGINE_API SceneSerializer
{
  public:
    /// Loads, validates, and migrates a serialized scene document.
    /// @param path Source scene file path.
    /// @return Parsed document and warnings, or an error when loading fails.
    SceneDocumentResult Load(const std::filesystem::path &path) const;

    /// Writes a document through a temporary file and atomically replaces the destination.
    /// @param path Destination scene file path.
    /// @param document Scene data to serialize.
    /// @param error Receives a diagnostic when serialization or replacement fails.
    /// @return True when the complete document was saved successfully.
    bool SaveAtomic(const std::filesystem::path &path, const SceneDocument &document,
                    std::string &error) const;
};
} // namespace Engine
