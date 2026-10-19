# Snapshot Analysis workspace

Snapshot Analysis captures one completed render from an authored scene camera
and then works from the captured CPU-owned data. Inspection, pinning, material
isolation, ROI analysis, comparison, and exports do not update the scene or
render another frame. A capture becomes visible as a frame tab only after all
required planes and metadata have been produced successfully.

## Capture contract

Capture starts from one frozen set of scene and render inputs: the authored
camera and projection, scene transforms, mesh/submesh and material assignments,
material values and texture/resource identities, lights, render settings, and
output dimensions. The default dimensions are the authored camera resolution.
A frame-local resolution override affects only that frame. The capture service
must own or retain immutable copies of everything the renderer needs after
capture starts; it must not keep consulting mutable editor selections or
authored scene state. Progress and cancellation belong to the capture request.
Cancellation or any failure discards every incomplete plane and publishes no
frame.

The renderer's `RenderOutput` remains an owning, temporary result of a final
render. The workspace representation owns a complete frame and exposes only
read-only plane access after publication. Moving or closing a tab releases the
frame when no other workspace state refers to it.

## Plane and pixel contract

All seven planes have identical nonzero width and height and address the same
source pixel. Coordinates use an origin at the upper-left; `x` increases to the
right and `y` downwards. Rows are tightly packed in top-to-bottom order, so the
sample index is `y * width + x`. Canvas zoom and pan transform display
coordinates only. They do not resample pixels or change pinned-pixel and ROI
coordinates.

| Plane | Storage and channels | Interpretation |
| --- | --- | --- |
| `final_color` | Float32 RGBA | Linear scene-referred color and opacity, after the optional final-color hook. RGB uses the captured color-space interpretation. |
| `depth` | Float32 Z | Normalized device depth in `[0, 1]`; background is `1`. |
| `world_normal` | Float32 XYZ | Normalized world-space shading normal. |
| `specular_lighting` | Float32 RGB | Direct and environment specular contribution, before the final-color hook. |
| `diffuse_lighting` | Float32 RGB | Direct diffuse, ambient/image-based diffuse, and emissive contribution, before the final-color hook. |
| `material_instance_id` | UInt32 ID | Capture-local material-instance identity. `0` means no material/background. Nonzero values are keys into the frame manifest. IDs are never converted to floating point. |
| `overdraw` | UInt32 count | Number of eligible scene-geometry fragments covering the pixel before depth testing. |

The material manifest maps each nonzero ID to the captured entity identity,
submesh index, material slot, material identity, and captured material values
and resource identities. It contains values and stable identities, not live
object pointers. One manifest entry represents one entity/submesh/material
assignment in this capture; repeated pixels and draw fragments use that same
ID. Unmapped nonzero IDs make a frame invalid.

Overdraw is measured at one sample per output pixel with multisampling disabled.
The count includes fragments from scene geometry that pass viewport clipping
and material alpha clipping, including opaque, masked, and blended transparent
materials. It counts those fragments before the depth test, so hidden and
depth-rejected fragments contribute; transparent fragments contribute when
their captured opacity is greater than zero. The count excludes fragments
discarded by alpha clipping, shadow-map and other non-color passes, background
or sky rendering, and editor-only content such as selection outlines, gizmos,
and UI. Counts are exact UInt32 values; a count overflow fails capture instead
of wrapping. Pixels with no eligible fragments contain zero.

Float values retain their captured IEEE-754 values. Analysis treats non-finite
samples as invalid, excludes them from histograms and comparison statistics,
and reports the excluded sample count. Material IDs and overdraw counts are
integer data and remain exact through capture, analysis, and persistence.

## Interpretation metadata

Each frame records enough metadata to explain its samples without reopening a
scene: capture timestamp; source scene identity; authored camera identity,
transform, projection type and parameters; authored and output resolution;
render settings; color primaries, transfer function, scene-linear or calibrated
unit interpretation; optional photometric calibration; and the entity, mesh,
submesh, material, and texture identities and captured values needed by the
inspectors. Metadata is a captured value snapshot. It is never a recipe for
reconstructing or changing the active scene.

Luminance uses linear Rec. 709 weights `(0.2126, 0.7152, 0.0722)`. The unit is
nits only when the frame carries valid photometric calibration for its color
values. Otherwise charts label luminance as scene-linear relative luminance.
Pixel inspection shows raw stored values; any display transform or heatmap
range affects presentation only.

## Comparison contract

Wipe comparison requires equal image dimensions. Numeric comparison also
requires matching plane semantics, channel layout, scalar format, and compatible
color interpretation and units. It never resamples or silently reinterprets a
buffer. Differences in camera or scene metadata are shown to the user but do
not by themselves make image-space comparison ineligible. A failure reports
the specific incompatibility.

For reference frame A and comparison frame B, channel absolute error is
`abs(B - A)`. Its scalar display map is mean absolute RGB error. RGB RMS
magnitude is `sqrt((dR*dR + dG*dG + dB*dB) / 3)` and is the default scalar map.
Relative luminance error is `abs(YB - YA) / max(abs(YA), 1e-6)` using linear
Rec. 709 luminance; it is available only for compatible linear `final_color`
planes. Swapping A and B changes the reference direction for relative error.

Pinned pixels expose unmodified raw values. Mean, maximum, and RMS statistics
use valid samples only and report how many invalid samples were excluded. The
user-adjustable heatmap range clamps the displayed heatmap only; it never
changes stored difference values or statistics. A separate legend identifies
the heatmap range and metric.

## Persistence contract

OpenEXR is the captured-data payload. It contains one named part per plane,
with float planes stored as FLOAT channels and `material_instance_id` and
`overdraw` stored as UINT channels. Stable part/channel names and interpretation
metadata travel with the payload. The reader validates required parts, channel
types and layouts, dimensions, metadata, and material-manifest references
before returning a frame. Missing, duplicate, unsupported, malformed, or
corrupt input returns a path-specific error and no partial frame.

An `.aframe` document is versioned JSON that references EXR payloads using
relative paths by default. It stores open frame tabs, selected frame and AOV,
pinned pixels, source-space ROI, zoom and pan, and comparison pair, metric, and
wipe position. Saving writes a complete valid document atomically. Opening an
EXR creates a new frame tab; opening an `.aframe` creates a new workspace tab
set. Neither operation loads source scene assets or changes the active scene.
All referenced payloads are validated before the new tabs are committed to the
workspace, preserving existing tabs on failure. Capture does not auto-save;
save/export uses an explicit destination and confirms overwrite. Closing an
unsaved workspace offers save, discard, or cancel.
