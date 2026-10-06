# glTF Material Coverage

The importer accepts materials only when every used field has a matching renderer path. It rejects unsupported material features with the source path and a field-specific error rather than silently rendering a partial material.

| glTF field | Status | Renderer behavior |
| --- | --- | --- |
| `baseColorFactor` | Supported | Multiplied by the base-color texture and used for alpha classification. |
| `baseColorTexture` | Supported for `TEXCOORD_0` and `TEXCOORD_1` | Sampled with glTF sampler state and optional `KHR_texture_transform`. |
| `metallicFactor`, `roughnessFactor` | Supported | Applied by the GGX renderer. |
| `metallicRoughnessTexture` | Supported for `TEXCOORD_0` and `TEXCOORD_1` | Metallic uses B; roughness uses G. |
| `normalTexture` | Supported for `TEXCOORD_0` and `TEXCOORD_1` | Sampled with the tangent basis and glTF normal scale. |
| `occlusionTexture`, `occlusionStrength` | Supported for `TEXCOORD_0` and `TEXCOORD_1` | Multiplies the renderer's ambient term. |
| `emissiveTexture` | Supported for `TEXCOORD_0` and `TEXCOORD_1` | Sampled in sRGB and multiplied by the emissive factor. |
| `alphaMode`, `alphaCutoff`, `doubleSided` | Supported | Forward and shadow paths apply the corresponding policy. |
| `emissiveFactor` | Supported | Added to the material output. |
| `KHR_texture_transform` | Supported on standard material texture slots | Per-slot offset, rotation, scale, and UV set override are applied in the shader. |
| `TEXCOORD_2` and above | Rejected | Vertex data currently exposes two texture-coordinate sets. |

This table describes the intentionally supported subset. Unsupported values are never silently dropped at import.
