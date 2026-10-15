# Final rendering and output planes

`RenderSystem::RenderFinal` renders one scene-camera view to an offscreen target
at the requested width and height. The call submits a complete frame and
returns a CPU-owned `RenderOutput`. Final rendering does not include editor
gizmos, camera frusta, or other debug geometry. Editor viewport rendering keeps
its viewport dimensions and debug overlays.

Interactive frame submission accepts multiple `ViewportBinding` values. Each
view keeps its own camera and framebuffer dimensions. `Mode::Editor` draws
editor debug geometry, `Mode::Preview` renders a single-color camera preview
without editor debug geometry, and `Mode::Final` uses the final-render AOV
attachments. Invalid bindings, including targets with no framebuffer or zero
dimensions, are skipped. Preview resolution is controlled by its target size;
the renderer derives the camera aspect ratio from those target dimensions.

## Output contract

The output contains five Float32 planes in this canonical order. Pixel rows are
tightly packed, row-major, and top-to-bottom. The listed channel names are
stable and are exposed by `RenderAovDescriptor` for image writers.

| Identifier | Name | Channels | Meaning |
| --- | --- | --- | --- |
| `FinalColor` | `final_color` | R, G, B, A | Linear scene-referred color; before post-processing, RGB is the sum of diffuse and specular lighting, and A is material opacity. |
| `Depth` | `depth` | Z | Normalized device depth in [0, 1]; background is 1. |
| `WorldNormal` | `world_normal` | X, Y, Z | Normalized world-space shading normal, including the material normal map. |
| `SpecularLighting` | `specular_lighting` | R, G, B | Direct and environment specular contribution. |
| `DiffuseLighting` | `diffuse_lighting` | R, G, B | Direct diffuse, ambient and diffuse environment contribution, plus emissive. |

`RenderOutput::GetPlanes()` enumerates the available owned planes and
`GetPixels(RenderAov)` selects one. A successful final render always contains
all five planes. Invalid scene-camera parameters, zero dimensions, an active
frame, target allocation failure, render-graph failure, readback failure, or a
post-process error fails the operation; partial or placeholder outputs are
not returned. A plane's sample span remains valid until its owning
`RenderOutput` is destroyed or move-assigned.

## Post-processing and color

The optional `FinalColorPostProcess` runs after GPU readback and may modify the
RGBA final-color plane in place. The lighting, normal, and depth planes remain
pre-post-process. Final color stays linear and scene-referred; neither the
renderer nor the hook applies an implicit tone map or display transfer. Hooks
must preserve linear semantics. Image exporters can apply a display transform
when writing a display-referred format such as PNG; multilayer EXR can retain
the float samples.

The output dimensions determine camera aspect ratio for final rendering. A
camera's pose and projection parameters are supplied through
`Renderer::CameraView`; scene-camera resolution settings remain editor camera
metadata and do not override an explicit final-output size.
