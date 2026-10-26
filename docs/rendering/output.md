# Render output and image export

`Engine::RenderSystem::RenderFinal` returns an owning `Engine::RenderOutput` with five tightly packed, top-to-bottom float32 planes at the requested camera resolution. The output owns its plane storage; spans returned by its getters remain valid until the output is destroyed or move-assigned.

| AOV | EXR part name | Channels | Meaning |
| --- | --- | --- | --- |
| `FinalColor` | `final_color` | `R`, `G`, `B`, `A` | Linear scene-referred final color after SSAO, camera GPU color effects, and the optional final-color hook. Alpha includes camera bloom coverage. |
| `Depth` | `depth` | `Z` | Normalized device depth in `[0, 1]`; background is `1`. |
| `WorldNormal` | `world_normal` | `X`, `Y`, `Z` | Normalized world-space shading normal. |
| `SpecularLighting` | `specular_lighting` | `R`, `G`, `B` | Direct and environment specular lighting. |
| `DiffuseLighting` | `diffuse_lighting` | `R`, `G`, `B` | Direct diffuse, ambient and image-based diffuse lighting, and emissive contribution. |

SSAO runs before forward lighting and attenuates indirect diffuse, so `FinalColor` and `DiffuseLighting` include its effect. The other lighting and geometric AOVs are unchanged. The ordered camera color effects and optional final-color hook affect only `FinalColor`. Failed renders return an error instead of partial or placeholder output.

Transparent materials are composited source-over in `FinalColor`, `SpecularLighting`, and `DiffuseLighting` using the same material opacity. Before color effects, the premultiplied `FinalColor` RGB equals the sum of the two lighting planes. `Depth` and `WorldNormal` identify the nearest visible contributing surface; these geometric values are not blended.

The CPU `FinalColor` plane is returned as straight-alpha RGBA. GPU-composited RGB is unpremultiplied during readback, allowing image exporters and CPU final-color hooks to apply opacity once when compositing over a background. Before color effects, multiplying the returned `FinalColor` RGB by its alpha recovers the sum of the two lighting planes.

## PNG

`RenderOutputExporter::WritePng` writes the `final_color` plane as 8-bit RGBA PNG. It applies a fixed Reinhard mapping `x / (1 + x)` to each finite RGB sample, then the sRGB transfer function and 8-bit quantization. Negative RGB values map to zero. Alpha is clamped to `[0, 1]` and quantized without a transfer function. PNG therefore stores a display-oriented result; it does not preserve the source float values.

The editor scene viewport and its in-scene camera picture-in-picture (PiP) render into a linear floating-point color target, composite transparent surfaces there, then apply the same Reinhard and sRGB display transform used by PNG output. This keeps live viewport color consistent with exported images without transforming individual transparent fragments before blending. Full-resolution float AOVs remain linear; the display transform affects only the viewport color output and is not applied to depth, normals, or lighting AOVs.

The export reports invalid dimensions or samples, unsupported paths, and OpenImageIO create, open, write, or close failures through `Engine::Status`.

`RenderOutputExportService` runs camera output export asynchronously. Start a
PNG or selected-AOV EXR request on the render thread, call `Update` on later
frames to poll GPU readback, then inspect `GetStatus` for writing progress,
completion, or a diagnostic message. Once readback completes, the service
passes an owning `RenderOutput` to its background OpenImageIO worker; the
worker does not access scene, renderer, or UI objects. Call `Shutdown` on the
render thread before the associated `RenderSystem` or graphics device is
destroyed. Shutdown cancels a pending GPU ticket and joins the writer, allowing
already queued file writes to finish safely.

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
