# Analytical Frames

The editor's **Analytical Frame** panel captures one authored scene camera and one material target, then renders the selected BRDF models sequentially. Every variant uses a copied scene, the same camera, lights, texture identities, render settings, and output size. Only the BRDF model changes. Roughness and GTR distribution overrides are frame-local and apply equally to every variant; they do not modify scene or material assets.

Capture defaults to the selected camera's authored output resolution. Enable **Resolution override** to choose a different frame size. The panel reports progress and lets you cancel between variants. If a render or measurement fails, or capture is cancelled, the panel discards all partial results.

Each model stores all five linear float AOVs: final color, depth, world normal, specular lighting, and diffuse lighting. It also stores the BRDF response curve and its peak, mean, and angular integral, plus scene mean and peak luminance. Captured frames are immutable snapshots; changing the active scene does not update them.

## Static inspection

Use the viewer controls to select an AOV or BRDF model, or compare two models with an A/B wipe. Zoom and pan affect only the displayed captured pixels. Click a pixel to pin its coordinates and inspect the raw AOV samples. Relative luminance error is available for compatible final-color captures only when no custom post-process callback was used.

For each pixel, the relative error is

```text
|YB - YA| / max(|YA|, 1e-6)
```

where `YA` and `YB` are linear Rec. 709 luminance values computed with weights `(0.2126, 0.7152, 0.0722)`. The inspector reports the raw error. The displayed heatmap clamps values to `[0, 1]`.

**Export chart PNG** writes the analytic BRDF response curves. **Export measurements JSON** writes the measurements and captured settings.

## Save and reopen projects

Use **Save .aframe** to write a self-contained project containing a versioned JSON manifest, the captured AOV buffers, measurements, relevant scene/material/light/camera/render settings, frame overrides, and viewport state. The archive includes the authored scene document and material texture identities, while the captured buffers remain usable without resolving the original scene or assets.

Use **Open .aframe** to inspect a saved project in the static viewer. Opening an archive validates the full manifest, captured planes, dimensions, and checksums before replacing the displayed frame. It does not load or change the active scene. Unsupported versions, incomplete data, corruption, and invalid viewport state are reported as errors; an invalid archive never opens a partial session.
