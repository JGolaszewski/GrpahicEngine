# PRISM generated NDF shader math

`brdf_common.hlsli` provides a reusable degree-8 real Zernike evaluator for the
PRISM generated NDF path. It expects 45 coefficients packed consecutively into
12 `float4` values (the final three lanes are ignored), ordered by increasing
degree `n = 0..8` and, for each degree, `m = -n, -n+2, ..., n`. Negative `m`
selects the sine component; non-negative `m` selects cosine, with `m = 0`
being radial. The basis is unnormalised, matching the PRISM decoder convention.

For `x² + y² <= 1`, the basis is evaluated as
`P_k^(0,|m|)(2r² - 1) * component((x + iy)^|m|)`, where
`k = (n - |m|) / 2`. The Jacobi polynomial is computed by recurrence. This
avoids factorials, multinomial evaluation, `atan2`, and trigonometric functions
in the pixel path. `PrismZernikeNdf` accumulates the linear combination, adds
the caller-sampled **signed** residual, clamps the sum at zero, masks outside
the unit disk, and multiplies by the supplied projected-energy normalization.
The caller owns residual texture sampling and must use identical coordinates,
filtering, coefficient order, residual sign, and normalization in both material
shader variants.

## Integration boundary

The current D3D11 material constant buffer has no PRISM coefficients or
normalization value, and the current shader resource slots are already used by
shadow, environment lighting, and ambient occlusion. This helper therefore does
not declare a resource register or change either material shader's dispatch.
Core/SceneRenderer must define the matching coefficient packing and a free
residual SRV binding before a model value can safely select this evaluator.
The environment prefilter and BRDF integration LUT also remain on their
existing analytic path; a PRISM-specific IBL cache requires the separate
environment pipeline change.

The shared function compiles as part of both existing pixel shaders. This is a
syntax/build check only: numerical CPU/GPU parity, actual residual sampling,
normalization integration, direct-light response, and IBL parity require the
matching payload producer and GPU validation fixture.
