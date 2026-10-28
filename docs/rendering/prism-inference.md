# PRISM inference service

The public API at
`GraphicEngineCore/modules/runtime/include/PrismNdfInferenceService.h` is
available in both runtime-enabled and standard builds. Construct
`Engine::PrismNdfInferenceService` with the staged model directory, embedded
`python.exe`, and `prism_tokenizer_worker.py` paths. `Start(prompt, directional)`
returns a request ID. Call `Poll(id)` from the Editor/UI thread to read
thread-safe progress snapshots and the completed immutable
`std::shared_ptr<const Engine::PrismNdfData>`. `Cancel(id)` requests cancellation,
which is observed between tokenization and each ONNX stage. The service never
calls UI callbacks from its worker. `PrismNdfInferenceStatus` retains the prompt,
directional flag, and pinned model provenance for persistence with the material.

When `ENGINE_ENABLE_PRISM_INFERENCE=OFF`, the same public API is linked and
`Start` returns a diagnostic directing users to enable the CMake option and
`prism-inference` vcpkg feature and use a package with runtime assets. Enabled
builds use Python only for the JSON-lines tokenizer protocol; encoder,
retriever, and decoder execution all use native ONNX Runtime. One tokenizer
process and the three model sessions are created and tensor-contract-checked
once on the service worker thread, then reused for all requests during that
service lifetime. The decoder's
`[1,4096]` grid is shape-checked, fitted to degree-8 Zernike coefficients with
signed residuals, and normalized before becoming visible through `Poll`.

The model interface sequence is fixed by the checked-in manifest:

1. Tokenizer emits `input_ids` and `attention_mask`, each `int64[1,128]`.
2. The E5 encoder produces `normalized_embedding`, `float32[1,1024]`.
3. The retriever consumes the embedding and `directional: bool[1]`, producing
   `predicted_latent`, `float32[1,16]`.
4. The VAE decoder produces `ndf_grid_flat`, `float32[1,4096]`.
5. Native code validates the grid, fits the existing degree-8 Zernike basis,
   stores the signed residual samples, and computes projected-energy
   normalization before publishing the immutable result.

Progress states are `Queued`, `Tokenizing`, `Encoding`, `Retrieving`, `Decoding`,
`Fitting`, then one terminal state (`Completed`, `Failed`, or `Cancelled`). A
request failure includes an actionable diagnostic in `message`; failed assets,
worker protocol errors, ONNX errors, and invalid output tensors do not expose a
partial result. Cancellation is cooperative between stages; an ONNX call that
is already executing completes before cancellation is observed.

The worker protocol tests use a stub tokenizer and run without model assets:

```powershell
python scripts/tests/test_prism_tokenizer_worker.py
```

The pinned tokenizer golden test additionally requires `tokenizers==0.21.4`
available to Python and `ENGINE_PRISM_TOKENIZER_JSON` pointing at the staged
`models/prism/tokenizer.json`:

```powershell
$env:ENGINE_PRISM_TOKENIZER_JSON = "out/prism-runtime/models/prism/tokenizer.json"
python scripts/tests/test_prism_tokenizer_golden.py
```
