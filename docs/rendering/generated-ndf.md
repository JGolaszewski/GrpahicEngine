# Generated PRISM NDFs

PRISM is an optional generated normal distribution function for a material. The render payload is self-contained: a fixed degree-8 Zernike coefficient vector, a signed 64×64 correction grid, a projected-energy normalization factor, and versioned generation provenance. Saved materials do not need the model package or tokenizer at render time.

## Payload contract

The coefficient ordering is the real Zernike sequence for radial order `n = 0..8` and azimuthal order `m = -n, -n+2, …, n`, for 45 values total. The shader reconstructs the polynomial base, adds the signed residual sampled over the unit disk, clamps the result to nonnegative values, masks points outside the disk, and multiplies by the stored normalization. The residual is row-major R32F data and is sampled linearly in the material tangent frame.

The stored normalization scales the projected-disk integral of `D` to one. This is the NDF normalization contract; it does not independently calibrate PRISM's learned shape as a complete BRDF. Direct and environment evaluation must use the same distribution and the engine's existing Fresnel and Smith geometry conventions.

## Serialization and compatibility

Scene and `.material` documents use schema version 9 for `brdfModel: "prismNdf"` and the nested `generatedNdf` object. It stores grid size, degree, data version, 45 coefficients, 4096 signed residual values, normalization, directional setting, prompt reference, and model/exporter versions. The loader rejects unsupported payload versions, incorrect array lengths, non-finite samples, and non-positive normalization. Scene schemas 1 through 8 continue to load with their existing analytic material defaults.

The prompt is provenance only. It is not sent to rendering shaders. Loading and rendering stored generated materials remain independent of ONNX Runtime, Python, model files, and network access.

## Generation and release

The Editor generation flow uses the Python tokenizer worker only to produce E5 token IDs and attention mask. C++ ONNX Runtime executes the pinned E5, retriever, and decoder graphs. Generation is asynchronous and produces an Inspector preview before Apply; Apply changes the selected material as a single undoable edit. Discard and failed generation leave the material unchanged.

Release model/runtime packaging and upstream provenance are documented in [PRISM model assets](../prism-models.md). The generated Zernike evaluator's recurrence and shader binding contract are documented in [PRISM shader math](prism-ndf-shader.md).
