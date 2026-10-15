# Scene cameras

Scene cameras are authored scene entities with a transform and projection settings. Their output resolution defaults to
1920 x 1080 and can be set to 720p (1280 x 720), 1080p (1920 x 1080), 1440p (2560 x 1440), 4K UHD (3840 x 2160),
or custom positive dimensions in the Inspector. Resolution is persisted with the camera; older scene files without
these fields load as 1920 x 1080.

The editor navigation camera is editor-owned view state. It does not appear in the scene hierarchy or scene files.
Camera frustums are editor debug geometry. They use each visible scene camera's transform, field of view, clip planes,
and output aspect ratio.
