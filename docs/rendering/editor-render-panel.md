# Editor render panel

The editor's **Render** panel provides a camera-based preview and image export workflow. Open it from the Window menu if it is not visible in the current docking layout.

1. Select a visible scene camera from **Scene camera**. The panel shows that camera's configured output dimensions.
2. Choose **Render Preview** to render at the camera's configured resolution. The preview is fitted to the panel while preserving its aspect ratio. Preview RGB uses the same fixed Reinhard tone mapping and sRGB conversion as PNG output.
3. Choose the AOVs to include in a multipart EXR. The canonical choices are Final Color, Depth, World Normal, Specular Lighting, and Diffuse Lighting.
4. Choose **Save PNG...** to save the final-color image, or **Save EXR...** to save the selected AOVs. Both actions render the selected camera again before writing. The native save dialog confirms overwrites and suggests the matching extension.

The panel reports render, upload, and file-write errors inline. Cancelling a save dialog does not write a file. An empty EXR selection is rejected with a prompt to select at least one AOV.

When the selected entity has a camera, the editor viewport shows a clickable
low-resolution camera preview. Click it to open the **Camera Preview** viewer.
The viewer renders the current scene at that camera's configured dimensions
through the final-render path, which excludes editor debug geometry. Use
**Refresh** after editing the scene, **Fit** to restore aspect-preserving fit,
and **1:1** or the mouse wheel to change zoom. Scrollbars pan a zoomed image.
Closing the viewer leaves the editor viewport available for scene navigation
and editing.

## Manual acceptance

- Select a camera entity in the hierarchy. Its viewport PiP opens the full viewer with the camera's configured output dimensions and without editor gizmos or debug geometry.
- Change a scene object, click **Refresh**, and confirm the viewer updates while the editor viewport remains open and usable.
- Resize the viewer and use **Fit**, **1:1**, mouse-wheel zoom, and scrollbars. The fitted image keeps its aspect ratio and an enlarged image can be panned in both directions.
- With at least two visible cameras, changing **Scene camera** updates the displayed output dimensions; rendering shows the selected camera's framing.
- **Render Preview** displays the image at the configured aspect ratio, including after resizing or docking the panel.
- Toggle individual AOVs and save EXR; the resulting multipart file contains only the checked AOV parts. With no AOV selected, EXR save reports the selection requirement.
- Save PNG and EXR through their native dialogs. Check the suggested extensions, overwrite confirmation, completion path, and visible error feedback for an unwritable destination.
