# Material binding ABI

The standard graphics pipeline uses these shader registers. Keep the material, scene, and per-object layouts aligned with this table when adding resources.

| Register | Resource | Owner |
| --- | --- | --- |
| `b0` | Per-object transforms and render flags | `SceneRenderer` |
| `b1` | Material factors | `MaterialSystem` |
| `b2` | Light data | `SceneRenderer` |
| `t0` / `s0` | Base-color map and sampler | `MaterialSystem` |
| `t1` / `s1` | Tangent-space normal map and sampler | `MaterialSystem` |
| `t2` / `s2` | Metallic-roughness map and sampler | `MaterialSystem` |
| `t6` / `s6` | Shadow-map array and comparison sampler | `SceneRenderer` |
| `b4` | Camera position | `SceneRenderer` |
| `t4` / `s4` | Occlusion map and sampler | `MaterialSystem` |
| `t5` / `s5` | Emissive map and sampler | `MaterialSystem` |

The `b1` material block is 304 bytes. Its first six 16-byte slots hold base color, emissive/metallic, surface parameters, specular parameters, model flags, and alpha parameters. The next two slots select one of eight vertex UV sets for the five texture slots. A texture parameter slot stores normal scale and occlusion strength. Five 2D transforms follow as rows and offsets. `ModelFlags.y` selects the BRDF distribution; the low byte of `.z` selects dielectric specular handling and bit 8 of `.z` selects the material workflow. `ModelFlags.w` stores the alpha mode in its low byte and the double-sided flag in bit 8. `AlphaParams.x` stores the masked-material cutoff. Keep `MaterialGpuAbiVersion` synchronized with this layout.

For `KHR_materials_pbrSpecularGlossiness`, the base-color slot and texture carry diffuse factor and diffuse texture; the specular parameters carry `specularFactor`; the metallic-roughness slot and texture carry the specular-glossiness data. The specular-glossiness texture is uploaded as sRGB so its RGB channels decode to linear F0 while alpha remains linear glossiness. The workflow flag selects this interpretation, while the BRDF selector independently chooses Legacy, GGX, or GTR shading.

Color and emissive images use the sRGB texture format. Normal and other data images use linear UNORM. Texture uploads decode to RGBA8 and build a full mip chain; imported glTF sampler filters and wrap modes are applied. Missing color/data maps use opaque white; missing normal maps use the tangent-space neutral normal `(0.5, 0.5, 1.0)`.
