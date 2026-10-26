# Final rendering and output planes

`RenderSystem::BeginFinalRender` renders one scene-camera view to an offscreen
target at the requested width and height and returns a ticket after submitting
GPU copies to staging textures. Call `PollFinalRender` on the render thread in
later frames; it returns an empty optional until GPU work completes, then
returns a CPU-owned `RenderOutput`. Only one final-render ticket may be active
per render system. `RenderSystem::RenderFinal` remains available as a blocking
compatibility wrapper. Final rendering does not include editor gizmos, camera
frusta, or other debug geometry. Editor viewport rendering keeps its viewport
dimensions and debug overlays.

`RenderOutputExportService` starts PNG or selected-AOV EXR exports from a final
render. Its `Update` method polls readback on the render thread and queues
CPU-owned pixels for a background OpenImageIO writer. The service reports
readback, writing, completion, and failure through request status. Call
`Shutdown` before destroying its render system or graphics device; this
cancels pending readback and joins the writer after queued writes finish.

Interactive frame submission accepts multiple `ViewportBinding` values. Each
view keeps its own camera and framebuffer dimensions. `Mode::Editor` draws
editor debug geometry, `Mode::Preview` renders a single-color camera preview
without editor debug geometry, and `Mode::Final` uses the final-render AOV
attachments. Invalid bindings, including targets with no framebuffer or zero
dimensions, are skipped. Preview resolution is controlled by its target size;
the renderer derives the camera aspect ratio from those target dimensions.

An authored camera may carry an optional `CameraPostProcessing` component with
screen-space ambient occlusion settings and an ordered list of color effects.
The color list can be empty or contain multiple or repeated effects. Its
settings must be copied into each camera `ViewportBinding` or passed to
final-render/export calls. SSAO runs before forward shading and attenuates
indirect diffuse using per-view depth and interpolated world-space mesh normals. Color grading,
bloom, and FXAA run in linear color after forward shading and before display
encoding and final readback. Bloom adds a thresholded blurred highlight and
expands alpha with the glow. SSAO changes `FinalColor` and `DiffuseLighting`;
the depth, normal, and specular-lighting planes are unchanged.

SSAO quality selects 8, 16, or 32 depth/normal samples per pixel; depth bias
is measured in scene units. FXAA quality selects its edge-search span and its
relative edge threshold controls which contrast transitions are filtered.
Bloom uses a bright-pass downsample pyramid followed by a blended upsample
pyramid. Pyramid depth adapts to camera output dimensions and bloom radius;
Smooth Bloom enables tent-filtered upsampling.

The scene also stores ambient diffuse-light settings: enabled state, linear RGB
color, and intensity. Each authored camera may override these settings for its
own view; otherwise it inherits the scene value. Ambient light contributes to
diffuse shading, is attenuated by SSAO, and does not change direct or specular
lighting. Environment lighting remains a separate setting and is not enabled
implicitly by SSAO. Final-render and export requests accept the camera override
so the camera output matches its preview.

## Output contract

The output contains five Float32 planes in this canonical order. Pixel rows are
tightly packed, row-major, and top-to-bottom. The listed channel names are
stable and are exposed by `RenderAovDescriptor` for image writers.

| Identifier | Name | Channels | Meaning |
| --- | --- | --- | --- |
| `FinalColor` | `final_color` | R, G, B, A | Linear scene-referred color after SSAO and camera GPU color effects; before color effects its RGB is the sum of AO-adjusted diffuse and specular lighting. Alpha starts as material opacity; bloom may expand it, and the optional CPU final-color hook may modify RGBA. |
| `Depth` | `depth` | Z | Normalized device depth in [0, 1]; background is 1. |
| `WorldNormal` | `world_normal` | X, Y, Z | Normalized world-space shading normal, including the material normal map. |
| `SpecularLighting` | `specular_lighting` | R, G, B | Direct and environment specular contribution. |
| `DiffuseLighting` | `diffuse_lighting` | R, G, B | Direct diffuse, scene ambient and diffuse environment contribution, plus emissive. |

`RenderOutput::GetPlanes()` enumerates the available owned planes and
`GetPixels(RenderAov)` selects one. A successful final render always contains
all five planes. Invalid scene-camera parameters, zero dimensions, an active
frame, target allocation failure, render-graph failure, readback failure, or a
post-process error fails the operation; partial or placeholder outputs are
not returned. A plane's sample span remains valid until its owning
`RenderOutput` is destroyed or move-assigned.

## Post-processing and color

The optional `FinalColorPostProcess` runs after GPU readback and may modify the
RGBA final-color plane in place, after any camera GPU effects. The diffuse
lighting plane includes SSAO but excludes the ordered color effects and CPU
hook; specular lighting, normal, and depth planes remain unprocessed. Final color stays linear and
scene-referred; neither the renderer nor the CPU hook applies an implicit tone
map or display transfer. Hooks must preserve linear semantics. Image exporters
can apply a display transform when writing a display-referred format such as
PNG; multilayer EXR can retain the float samples.

The output dimensions determine camera aspect ratio for final rendering. A
camera's pose and projection parameters are supplied through
`Renderer::CameraView`; scene-camera resolution settings remain editor camera
metadata and do not override an explicit final-output size.
