# PRISM NDF generation in the Inspector

Select a mesh with a reusable material asset and choose **PRISM generated NDF** in the Inspector. Enter a text description and select directional mode when the distribution should use PRISM's directional conditioning. The Editor starts an asynchronous request and polls immutable status snapshots on the UI thread; no worker callback accesses ImGui or material state.

Generation produces an inline NDF heatmap and a lit-sphere preview. Both are derived from the preview payload and do not change the assigned material or asset. **Cancel generation** requests cancellation and ignores a late result if the selection changed. **Discard preview** drops the preview while retaining the material's current saved payload.

**Apply to material** is the only operation that commits a generated preview. It saves the material document first, then updates every scene mesh using that shared material and records one undo step. A failed save or shared-user update rolls back the asset and scene copies and leaves the command history unchanged. Undo and redo save and propagate the corresponding complete material document. If the selected entity or material differs from the request's origin, Apply is rejected as stale.

The model runtime is optional. Builds without it keep ordinary material rendering and saved PRISM payloads available, while the Inspector reports that generation is unavailable.
