# Render output and image export

`Engine::RenderSystem::RenderFinal` returns an owning `Engine::RenderOutput` with five tightly packed, top-to-bottom float32 planes at the requested camera resolution. The output owns its plane storage; spans returned by its getters remain valid until the output is destroyed or move-assigned.

| AOV | EXR part name | Channels | Meaning |
| --- | --- | --- | --- |
| `FinalColor` | `final_color` | `R`, `G`, `B`, `A` | Linear scene-referred final color after the optional final-color hook. Alpha is material opacity. The hook must preserve linear values. |
| `Depth` | `depth` | `Z` | Normalized device depth in `[0, 1]`; background is `1`. |
| `WorldNormal` | `world_normal` | `X`, `Y`, `Z` | Normalized world-space shading normal. |
| `SpecularLighting` | `specular_lighting` | `R`, `G`, `B` | Direct and environment specular lighting. |
| `DiffuseLighting` | `diffuse_lighting` | `R`, `G`, `B` | Direct diffuse, ambient and image-based diffuse lighting, and emissive contribution. |

Lighting and geometric AOVs are captured before the final-color hook. The final-color plane reflects the hook's changes. Failed renders return an error instead of partial or placeholder output.

Transparent materials are composited source-over in `FinalColor`, `SpecularLighting`, and `DiffuseLighting` using the same material opacity, so the two lighting planes sum to the un-postprocessed final RGB. `Depth` and `WorldNormal` identify the nearest visible contributing surface; these geometric values are not blended.

## PNG

`RenderOutputExporter::WritePng` writes the `final_color` plane as 8-bit RGBA PNG. It applies a fixed Reinhard mapping `x / (1 + x)` to each finite RGB sample, then the sRGB transfer function and 8-bit quantization. Negative RGB values map to zero. Alpha is clamped to `[0, 1]` and quantized without a transfer function. PNG therefore stores a display-oriented result; it does not preserve the source float values.

The export reports invalid dimensions or samples, unsupported paths, and OpenImageIO create, open, write, or close failures through `Engine::Status`.

## Multilayer OpenEXR

`RenderOutputExporter::WriteMultilayerExr` writes each selected AOV as a named part in one multipart EXR. Part names and channel names come from the stable descriptors in `RenderOutput`; every part stores float32 samples without tone mapping or other value conversion. The selected AOV list controls the part order and contents. Empty selections, duplicate selections, missing planes, malformed sample counts, unsupported AOVs, and file errors return an error.

Typical use keeps camera/render orchestration separate from file encoding:

```cpp
auto output = renderSystem.RenderFinal(scene, camera, width, height, postProcess);
if (!output) {
  return output.error();
}

auto pngStatus = Engine::RenderOutputExporter::WritePng(*output, pngPath);
auto exrStatus = Engine::RenderOutputExporter::WriteMultilayerExr(*output, selectedAovs, exrPath);
```
