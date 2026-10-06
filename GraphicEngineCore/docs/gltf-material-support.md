# glTF Material Coverage

The importer accepts materials only when every used field has a matching renderer path. It rejects unsupported material features with the source path and a field-specific error rather than silently rendering a partial material.

| glTF field | Status | Renderer behavior |
| --- | --- | --- |
| `baseColorFactor` | Supported | Multiplied by the base-color texture and used for alpha classification. |
| `baseColorTexture` | Supported for `TEXCOORD_0` | Sampled with linear filtering and repeat addressing. |
| `metallicFactor`, `roughnessFactor` | Supported | Applied by the GGX renderer. |
| `normalTexture` | Supported for `TEXCOORD_0`, scale `1.0` | Sampled with the tangent basis. |
| `alphaMode`, `alphaCutoff`, `doubleSided` | Supported | Forward and shadow paths apply the corresponding policy. |
| `emissiveFactor` | Supported | Added to the material output. |
| `metallicRoughnessTexture` | Rejected | Packed metallic/roughness channels are not sampled. |
| `occlusionTexture`, `occlusionStrength` | Rejected when the texture is present | Occlusion maps are not sampled. |
| `emissiveTexture` | Rejected | Emissive maps are not sampled. |
| Nonzero texture coordinate set | Rejected | Vertex data exposes only `TEXCOORD_0`. |
| Nondefault normal scale | Rejected | The material shader does not apply the glTF scale. |
| Sampler state other than linear filtering and repeat addressing | Rejected | Texture sampler state is not yet carried into the material binding. |

This table describes the intentionally supported subset. Unsupported values are never silently dropped at import.
