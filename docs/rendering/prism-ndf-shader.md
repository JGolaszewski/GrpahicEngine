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
remain functional. `PrismNdfMathTests` compares every CPU basis term to an
independent factorial definition and checks aggregate double-precision
reconstruction, disk boundary/exterior behavior, signed residual, clamp, and
normalization semantics. These CPU checks do not establish GPU parity.

The PRISM IBL loop samples the projected unit disk uniformly in area using
`r = sqrt((i + 0.5) / N)`. Its per-sample integrand is the BRDF half-vector
change of variables: `D(h) * G(v,l) * F(v,h) * (v·h) / ((n·v)(n·h))`.
Since the disk sample PDF is `1/π`, the final `π / N` multiplier is the
Monte Carlo area weight. With normalized `D`, it must not be removed or
duplicated. This derivation does not replace the required numerical comparison
against a high-sample reference.

## Open #190 verification criteria

- An independent D3D11 GPU-versus-double test must evaluate all 45 terms over
  interior, boundary, and exterior points, plus residual, clamp, and scale, with
  the issue's absolute `1e-4` and relative `5e-4` tolerances. Existing WARP
  rendering tests exercise the integrated material shaders but do not expose
  per-sample shader results for this comparison.
- Direct-light and high-sample PRISM IBL output still need numerical reference
  comparisons. Existing WARP tests establish response and repeatability only.
- The Intel Graphics and RTX 5080 adapters are present on the development host,
  but this checkout has no PRISM GPU microbenchmark or timestamp-query fixture.
  A measured per-pixel/IBL budget is therefore open until a hardware benchmark
  with warmup, repeated GPU timestamp samples, adapter/driver identification,
  and a documented scene/sample workload is available. WARP timings are not a
  substitute for that measurement.
