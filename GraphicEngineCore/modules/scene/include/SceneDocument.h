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
inline constexpr uint32_t CurrentSceneSchemaVersion = 1;

struct SceneMaterialDocument
{
    std::array<float, 4> baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> emissive{};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float gtrGamma = 2.0f;
    float ior = 1.5f;
    std::array<float, 3> specularF0 = {0.04f, 0.04f, 0.04f};
    std::string brdfModel = "legacy";
    std::string workflow = "metallicRoughness";
    std::string specularMode = "ior";
    std::string alphaMode = "opaque";
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    std::optional<std::string> baseColorTextureGuid;
    std::optional<std::string> normalTextureGuid;
};

struct SceneEntityDocument
{
    uint64_t id = 0;
    std::optional<uint64_t> parentId;
    std::string name;
    std::optional<std::string> modelGuid;
    std::optional<std::string> materialGuid;
    std::optional<std::string> primitiveType;
    std::optional<SceneMaterialDocument> material;
    std::vector<std::optional<SceneMaterialDocument>> materialOverrides;
    std::array<float, 3> position{};
    std::array<float, 4> rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
    bool forceWireframe = false;
    bool disableDepthTest = false;
    bool castShadows = true;
    bool receiveShadows = true;
};

struct SceneLightDocument
{
    uint64_t id = 0;
    std::array<float, 3> direction = {0.0f, -1.0f, 0.0f};
    std::array<float, 3> position = {0.0f, 3.0f, 0.0f};
    std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 20.0f;
    float innerAngleDegrees = 15.0f;
    float outerAngleDegrees = 25.0f;
    float shadowBias = 0.0015f;
    float shadowNormalBias = 0.0015f;
    bool usePosition = false;
    bool castShadows = true;
};

struct SceneCameraDocument
{
    uint64_t id = 0;
    std::array<float, 3> position{};
    std::array<float, 4> rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    float fovRadians = 0.785398163f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

struct SceneDocument
{
    uint32_t schemaVersion = CurrentSceneSchemaVersion;
    float unitsPerMeter = 1.0f;
    std::vector<SceneEntityDocument> entities;
    std::vector<SceneLightDocument> lights;
    std::vector<SceneCameraDocument> cameras;
};

struct SceneDocumentResult
{
    SceneDocument document;
    std::vector<std::string> warnings;
    std::string error;

    explicit operator bool() const noexcept
    {
        return error.empty();
    }
};

class ENGINE_API SceneSerializer
{
  public:
    SceneDocumentResult Load(const std::filesystem::path &path) const;
    bool SaveAtomic(const std::filesystem::path &path, const SceneDocument &document,
                    std::string &error) const;
};
} // namespace Engine
