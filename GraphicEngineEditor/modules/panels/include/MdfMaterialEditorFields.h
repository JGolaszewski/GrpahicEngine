#pragma once

#include <array>
#include <string_view>

/// MaterialProperties members represented by controls in the MDF inspector.
inline constexpr std::array<std::string_view, 21> MdfMaterialEditorFields = {
    "BaseColor",         "Emissive",
    "Metallic",          "Roughness",
    "GtrGamma",          "Ior",
    "SpecularF0",        "Model",
    "Workflow",          "SpecularMode",
    "AlphaMode",         "AlphaCutoff",
    "DoubleSided",       "BaseColorTexCoord",
    "NormalTexCoord",    "MetallicRoughnessTexCoord",
    "OcclusionTexCoord", "EmissiveTexCoord",
    "NormalScale",       "OcclusionStrength",
    "TextureTransforms"};
