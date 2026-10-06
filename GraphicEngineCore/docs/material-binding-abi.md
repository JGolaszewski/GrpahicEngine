# Material binding ABI

The standard graphics pipeline uses these shader registers. Keep the material, scene, and per-object layouts aligned with this table when adding resources.

| Register | Resource | Owner |
| --- | --- | --- |
| `b0` | Per-object transforms and render flags | `SceneRenderer` |
| `b1` | Material factors | `MaterialSystem` |
| `b2` | Light data | `SceneRenderer` |
| `t0` / `s0` | Base-color map and sampler | `MaterialSystem` |
| `t1` / `s1` | Tangent-space normal map and sampler | `MaterialSystem` |
| `t3` / `s3` | Shadow-map array and comparison sampler | `SceneRenderer` |
| `b4` | Camera position | `SceneRenderer` |

The `b1` material block is 96 bytes: five 16-byte slots for base color, emissive/metallic, surface parameters, specular parameters, model flags, and alpha parameters. `ModelFlags.w` stores the alpha mode in its low byte and the double-sided flag in bit 8. `AlphaParams.x` stores the masked-material cutoff. Keep `MaterialGpuAbiVersion` synchronized with this layout.

Color and emissive images use the sRGB texture format. Normal and other data images use linear UNORM. Texture uploads currently decode to RGBA8, use one mip level, sample linearly, and wrap in both UV directions. Missing base-color maps use opaque white; missing normal maps use the tangent-space neutral normal `(0.5, 0.5, 1.0)`.
