# Scene cameras

Scene cameras are authored scene entities with a transform and projection settings. The scene file preserves their name,
hierarchy, local transform, projection settings, output resolution, selection priority, and active state. New transform,
name, priority, and active fields are optional when loading older files and use the existing ECS defaults.

Output resolution defaults to 1920 x 1080 and can be set to 720p (1280 x 720), 1080p (1920 x 1080), 1440p
(2560 x 1440), 4K UHD (3840 x 2160), or custom positive dimensions in the Inspector. Resolution is persisted with the
camera; older scene files without these fields load as 1920 x 1080.

Add the optional **Post Processing** component to a camera in the Inspector to
configure screen-space ambient occlusion and its ordered color-effect stack.
SSAO uses radius and intensity settings and attenuates indirect diffuse before
forward lighting. The color stack supports color grading (exposure, contrast,
and saturation), bloom (threshold, intensity, and radius), and FXAA. These
settings run only on that authored camera's preview and final renders. With
SSAO disabled and an empty color stack, the camera output is unchanged. Older
scene files load with post-processing disabled.

The editor navigation camera is editor-owned view state. It does not appear in the scene hierarchy or scene files.
Camera frustums are editor debug geometry. They use each visible scene camera's transform, field of view, clip planes,
and output aspect ratio.

Selecting a scene camera shows a live, aspect-preserving picture-in-picture preview in the editor viewport. The preview follows scene edits while the editor navigation view remains available. Selecting another entity without a camera or clearing the selection hides the preview and releases its render target. The preview has a visible border; click it to open the full-resolution camera viewer. The viewer supports drag panning and mouse-wheel zoom.

PiP display scale and render quality are separate editor preferences in **Window > Settings**. Scale changes the on-screen preview size; quality selects the preview's render resolution relative to the camera output. Preferences persist locally.

## PiP acceptance check

1. Select a camera entity and confirm the viewport shows a small preview on its left side with the camera's aspect ratio.
2. Move or edit scene objects and confirm the PiP reflects the changes while the editor viewport remains visible.
3. Select a second camera and confirm the PiP switches to its framing; select a non-camera entity or clear selection and
   confirm the PiP disappears and its target is released.
4. Click the PiP and confirm it opens the full-resolution camera viewer. Click outside the PiP and confirm the editor
   viewport continues to respond to navigation and editing input.
