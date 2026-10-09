# Initial image import policy

The initial asset importer accepts PNG and JPEG image payloads through
`stb_image`, including image data embedded in a glTF file. Decoded pixels are
converted to four 8-bit channels (RGBA); alpha is preserved when present and
filled with fully opaque values otherwise. The decoder's source row order is
preserved, with no vertical flip. Texture-coordinate conventions and image
orientation are handled by the renderer rather than by changing imported
pixel data.

Decoded bytes are color-neutral. The image's glTF material usage determines
GPU interpretation: base-color and emissive maps use sRGB formats; normal,
metallic-roughness, and occlusion maps use linear formats. The same image may
be referenced in more than one semantic role, so color-space interpretation is
per texture view/material binding and must not mutate the shared CPU pixels.

Other image formats, unsupported glTF extensions, and decoder failures must
produce diagnostics that identify the source asset and image. They must not be
silently treated as valid empty textures.
