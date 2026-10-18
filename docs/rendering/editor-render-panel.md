# Editor camera previews

The editor shows a selected scene camera as a live picture-in-picture preview in the scene viewport. A gold border marks the preview over the editor image. Click it to open the **Camera Preview** viewer.

The full viewer renders the current scene at the camera's configured output dimensions through the final-render path, excluding editor debug geometry. Use **Refresh** after editing the scene, **Fit** to restore aspect-preserving fit, and **1:1** or the mouse wheel to change zoom. Drag the image to pan when it extends beyond the viewer; the hand cursor indicates that the image can move.

The viewer provides **Save PNG...** and **Save EXR...**. PNG contains final color. EXR contains the selected AOVs: Final Color, Depth, World Normal, Specular Lighting, and Diffuse Lighting. At least one AOV must be selected. The viewer reports render/readback progress, file writing, completion, and errors. Closing the viewer does not interrupt an accepted export.

Open **Window > Settings** to configure the PiP's displayed scale and render quality independently. Scale changes its size in the editor viewport; quality sets the render resolution as a percentage of the camera output. Debug renderer visibility options are in the same settings window. These preferences persist locally across restarts.

## Manual acceptance

- Select a scene camera and confirm the PiP updates with scene changes, shows its border, and opens the matching full viewer when clicked.
- In the viewer, check fit, 1:1, wheel zoom, drag panning, and the hand cursor over the image.
- Adjust PiP display scale and render quality separately in **Window > Settings**; confirm both survive an editor restart.
- Save PNG and EXR and verify camera dimensions and selected EXR AOVs.
- Import a model and texture from the Assets panel and confirm each appears in the asset list.
