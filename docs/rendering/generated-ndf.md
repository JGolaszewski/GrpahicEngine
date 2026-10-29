# Generated PRISM NDFs

PRISM is an optional generated normal distribution function for a material. The render payload is self-contained: a fixed degree-8 Zernike coefficient vector, a signed 128×128 correction grid, a projected-energy normalization factor, and versioned generation provenance. Saved materials do not need the model package or tokenizer at render time.

## Payload contract

The coefficient ordering is the real Zernike sequence for radial order `n = 0..8` and azimuthal order `m = -n, -n+2, …, n`, for 45 values total. The shader reconstructs the polynomial base, adds the signed residual sampled over the unit disk, clamps the result to nonnegative values, masks points outside the disk, and multiplies by the stored normalization. The residual is row-major R32F data and is sampled linearly in the material tangent frame. CPU payloads keep residuals in vector-backed storage to avoid inflating every scene and material object; a valid payload must contain exactly 16,384 samples.

The stored normalization scales the projected-disk integral of `D` to one. This is the NDF normalization contract; it does not independently calibrate PRISM's learned shape as a complete BRDF. Direct and environment evaluation must use the same distribution and the engine's existing Fresnel and Smith geometry conventions.

## Serialization and compatibility

Scene and `.material` documents use schema version 9 for `brdfModel: "prismNdf"` and the nested `generatedNdf` object. It stores grid size, data version 2, degree, 45 coefficients, 16,384 signed residual values, normalization, directional setting, prompt reference, and model/exporter versions. The loader rejects a missing payload for PRISM, a payload attached to another BRDF, unsupported dimensions/degrees/versions, incorrect array lengths, non-finite coefficients or residuals, and non-finite or non-positive normalization. Scene schemas 1 through 8 continue to load; legacy version-1 64×64 PRISM payloads are bilinearly resampled to the current 128×128 version-2 grid. Pre-existing material fields retain their version-specific defaults and PRISM is absent unless explicitly serialized in schema 9. Saving validates the full document before atomic replacement, so invalid generated data cannot replace an existing scene file.

Grid fitting uses streaming Givens QR in double precision, avoiding the squared condition number of normal equations. Rank-deficient or non-finite solves fail without changing the caller's output. Residuals are formed from the stored float coefficients so the persisted polynomial plus residual reproduces every in-disk input sample within float precision. Tests compare all basis terms to an independent factorial definition, compare a fitted coefficient vector with an independent least-squares oracle, and exercise legacy schema loads and malformed payload cases.

The prompt is provenance only. It is not sent to rendering shaders. Loading and rendering stored generated materials remain independent of ONNX Runtime, Python, model files, and network access.

## Generation and release

The Editor generation flow uses the Python tokenizer worker only to produce E5 token IDs and attention mask. C++ ONNX Runtime executes the pinned E5, retriever, and decoder graphs. Generation is asynchronous and produces an Inspector preview before Apply; Apply changes the selected material as a single undoable edit. Discard and failed generation leave the material unchanged.

Release model/runtime packaging and upstream provenance are documented in [PRISM model assets](../prism-models.md). The generated Zernike evaluator's recurrence and shader binding contract are documented in [PRISM shader math](prism-ndf-shader.md).
