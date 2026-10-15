# Scene cameras

Scene cameras are authored scene entities with a transform and projection settings. Their output resolution defaults to
1920 x 1080 and can be set to 720p (1280 x 720), 1080p (1920 x 1080), 1440p (2560 x 1440), 4K UHD (3840 x 2160),
or custom positive dimensions in the Inspector. Resolution is persisted with the camera; older scene files without
these fields load as 1920 x 1080.

The editor navigation camera is editor-owned view state. It does not appear in the scene hierarchy or scene files.
Camera frustums are editor debug geometry. They use each visible scene camera's transform, field of view, clip planes,
and output aspect ratio.

Selecting a scene camera shows a live, aspect-preserving low-resolution preview at the left side of the editor
viewport. The preview follows scene edits while the editor navigation view remains available. Selecting another entity
without a camera or clearing the selection hides the preview and releases its render target. Click the preview to open
the full-resolution camera viewer.

## PiP acceptance check

1. Select a camera entity and confirm the viewport shows a small preview on its left side with the camera's aspect ratio.
2. Move or edit scene objects and confirm the PiP reflects the changes while the editor viewport remains visible.
3. Select a second camera and confirm the PiP switches to its framing; select a non-camera entity or clear selection and
   confirm the PiP disappears and its target is released.
4. Click the PiP and confirm it opens the full-resolution camera viewer. Click outside the PiP and confirm the editor
   viewport continues to respond to navigation and editing input.
