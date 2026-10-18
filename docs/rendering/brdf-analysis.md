# BRDF analysis

The editor's **BRDF Analysis** panel compares the response of a selected scene material across the BRDF models supported by the renderer. The panel lists material-bearing scene entities and material override slots, and lets you choose one or more models before running the comparison.

## Captured comparison

Each run captures the authored scene, selected material parameters, camera, output dimensions, and final-color processing settings. The renderer then evaluates the selected BRDF variants from that capture. The only material input changed between variants is the BRDF model. Rendering and measurement use copied scene and material data; the active scene, material assets, and saved scene data are not edited.

The panel reports a response curve over light angle from 0 to 90 degrees, along with the curve's peak, mean, and angular integral. It also reports mean and peak luminance for the corresponding rendered scene. A comparison is displayed only after all requested variants render and all measurements succeed. If capture, rendering, or measurement fails, the panel displays the diagnostic and does not present a partial comparison as complete.

## Reading the measurements

The response curve fixes the view direction to the surface normal and sweeps the incident light angle uniformly from 0 to 90 degrees. Response values are Rec. 709 luminance of the unweighted BRDF value; the incident-angle cosine is not included. The angular integral uses radians. Scene luminance is measured from the linear final-color render output on a per-pixel Rec. 709 basis.

The curve describes the material's angular response in isolation, while scene luminance includes the captured scene geometry, lights, camera, and render inputs. Compare scene measurements only among variants from the same completed capture.

## Exporting a comparison

After a complete comparison appears, use **Export chart PNG** to save the displayed response curves with the same model colors and shared response scale. Use **Export measurements JSON** to save the captured scene document, source material values, camera and render settings (including output dimensions and active shadow limits), response samples and curve statistics, and mean and peak scene luminance for every variant. The PNG and JSON are produced from the comparison currently shown in the panel.

The JSON includes the full serialized scene, including authored lights and cameras. A custom final-color post-process is stored as a callback, so the report records whether one was present; its implementation cannot be represented as JSON.
