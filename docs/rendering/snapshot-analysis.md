# Snapshot Analysis workspace

Snapshot Analysis captures one completed render from an authored scene camera
and then works from the captured CPU-owned data. Inspection, pinning, material
isolation, ROI analysis, comparison, and exports do not update the scene or
render another frame. A capture becomes visible as a frame tab only after all
selected planes and metadata have been produced successfully. All seven supported
planes are selected by default; users may capture a nonempty subset.

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

## Workspace controls

Open Snapshot Analysis from the editor **Main Bar**. It replaces the editor
content area with a full-editor focused workspace rather than opening a floating
or dockable panel. With a frame open, the captured image stays in the main canvas
and its plane selector, comparison controls, pixel and material inspectors,
metadata, and ROI histogram stay in a scrollable side panel. The side panel has
a **Back to editor** action, and the Main Bar remains available with an **Exit
Snapshot Analysis** control. Exiting returns to the previous editor view, layout, and
focus; open frame tabs and their view state remain in memory for the next entry.
The active scene and authored state are preserved. Choose an authored camera
from the in-workspace selector. It defaults to the editor's selected camera,
then the highest-priority active camera, then the first valid camera in stable
entity order. Leave both
resolution override fields at zero to use the camera's authored resolution;
enter both dimensions to override the output size for that frame only. Capture
the selected planes with **Capture selected camera**. Seven plane toggles are
available, all enabled by default. At least one plane must stay selected.
Progress and failure messages appear beside the controls. Cancel discards the
pending capture without adding a frame tab. A new tab appears only after every
selected plane and any required material manifest are ready. Unselected planes
are absent from that frame and are never treated as zero-filled data.

Use **Open EXR** beside the capture controls to validate and add a saved frame tab, or **Open
.aframe** to validate its workspace document and every referenced EXR before
adding its frame tabs. A failure leaves the current tabs and scene untouched.
**Export frame EXR** writes the selected complete frame, including the exact
integer planes. **Save workspace .aframe** writes EXR payloads for any unsaved
frames and then writes the versioned workspace document. Exiting focused mode
does not discard open tabs or save them automatically. Save dialogs confirm
overwrites; capture itself never saves automatically.

Each frame tab retains its own frame data, selected available plane, pinned
pixel, ROI, zoom, and pan. Its plane selector lists only planes captured in
that frame. Opening a standalone EXR selects its first available plane;
workspace tabs retain their saved plane and are rejected if that plane is
missing from the referenced EXR. Integer material-ID and overdraw inspectors,
material isolation, histograms, and comparisons report when required planes
are unavailable. The canvas uses a display
transform for readability; the inspector reports raw stored pixel values.
Click a source pixel to pin it. The material inspector reads the capture's
manifest, and **Isolate pinned material** dims other pixels in the displayed
frame. This display filter never changes scene visibility or render state.

Use Shift-drag on the image to select a half-open source-pixel ROI. The
histogram panel reports RGB and linear Rec. 709 luminance distributions, the
valid and invalid pixel counts, the out-of-range value count, and whether
luminance uses nits or scene-linear relative units. Set the histogram minimum
and maximum to choose its bin range; values outside the range accumulate in
the end bins and remain counted as out of range. The image can be zoomed with
the mouse wheel and panned with the middle mouse button; both controls affect
only its display placement.

When at least two frame tabs are open, choose a reference frame to compare it
with the current frame. The comparison reports the B-minus-A direction,
statistics, and the raw metric at a pinned pixel. It offers mean absolute
error, RGB RMS, and relative luminance metrics; relative luminance is available
only for compatible linear `final_color` data. **Wipe A/B** is a display-only
split view that requires equal dimensions. The heatmap range clamps only its
display, never the stored values or statistics. Incompatible frames show the
analysis diagnostic and are not resampled or reinterpreted.

## Plane and pixel contract

All present planes have identical nonzero width and height and address the same
source pixel. A frame contains at least one plane; plane availability is
explicit. Coordinates use an origin at the upper-left; `x` increases to the
right and `y` downwards. Rows are tightly packed in top-to-bottom order, so the
sample index is `y * width + x`. Canvas zoom and pan transform display
coordinates only. They do not resample pixels or change pinned-pixel and ROI
coordinates.

| Plane | Storage and channels | Interpretation |
| --- | --- | --- |
| `final_color` | Float32 RGBA | Optional linear scene-referred color and opacity, after the optional final-color hook. RGB uses the captured color-space interpretation. |
| `depth` | Float32 Z | Optional normalized device depth in `[0, 1]`; background is `1`. |
| `world_normal` | Float32 XYZ | Optional normalized world-space shading normal. |
| `specular_lighting` | Float32 RGB | Optional direct and environment specular contribution, before the final-color hook. |
| `diffuse_lighting` | Float32 RGB | Optional direct diffuse, ambient/image-based diffuse, and emissive contribution, before the final-color hook. |
| `material_instance_id` | UInt32 ID | Optional capture-local material-instance identity. `0` means no material/background. Nonzero values are keys into the frame manifest. IDs are never converted to floating point. |
| `overdraw` | UInt32 count | Optional number of eligible scene-geometry fragments covering the pixel before depth testing. |

When `material_instance_id` is present, the material manifest maps each nonzero ID to the captured entity identity,
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

OpenEXR is the captured-data payload. It contains one named part for each present plane,
with float planes stored as FLOAT channels and `material_instance_id` and
`overdraw` stored as UINT channels. Stable part/channel names and interpretation
metadata travel with the payload. The reader requires at least one plane and validates unique part names, channel
types and layouts, dimensions, metadata, and material-manifest references
before returning a frame. Missing, duplicate, unsupported, malformed, or
corrupt input returns a path-specific error and no partial frame.

An `.aframe` document is versioned JSON that references EXR payloads using
relative paths by default. It stores open frame tabs, selected frame and plane,
pinned pixels, source-space ROI, zoom and pan, and comparison pair, metric, and
wipe position. Saving writes a complete valid document atomically. Opening an
EXR creates a new frame tab; opening an `.aframe` creates a new workspace tab
set. Neither operation loads source scene assets or changes the active scene.
Each tab's selected inspection plane must be present in its EXR. All referenced
payloads are validated before the new tabs are committed to the workspace,
preserving existing tabs on failure. Capture does not auto-save;
save/export uses an explicit destination and confirms overwrite. Closing an
unsaved workspace offers save, discard, or cancel.
