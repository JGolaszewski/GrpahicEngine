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

## D3D11 bindings and integration

`MaterialGpu` packs the 45 coefficients into the first 45 lanes of the 12
`float4` entries and places projected-energy normalization in `NdfParams.x`.
The direct-light shaders evaluate this payload and add the signed residual
sample at material SRV `t3`/sampler `s3`. PRISM-conditioned environment
integration samples the precomputed normalized distribution at `t6`/`s6` and
the active panorama at `t12`/`s12`.

Material resources occupy the contiguous SRV range `t0`–`t6`; scene resources
occupy `t7`–`t12`. This separation is required by the D3D11 NVRHI backend:
binding a set applies its complete contiguous SRV range, including null holes.
If a material resource extends through a scene slot, binding the material after
the scene set clears the scene resource at that slot. Keep the two ranges
disjoint when adding bindings. Sampler ranges follow the corresponding
resource assignments.

Both color pixel-shader variants implement the same direct-light and
PRISM-conditioned IBL math. The WARP rendering tests verify that coefficient
and signed-residual changes affect direct-light output, that the conditioned
environment contribution is repeatable, and that environment and AO bindings
remain functional. The independent basis and fit reference tests live in
`PrismNdfMathTests`; an exhaustive GPU-versus-double reference across boundary,
exterior, and residual cases remains a separate validation gap.
