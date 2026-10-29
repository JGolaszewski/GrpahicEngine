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
`NdfParams.y` is a float enable flag for the residual contribution. When it is
zero, the direct-light shaders use the coefficient basis alone and the
precomputed distribution texture is built without residuals. When enabled, direct lighting adds the signed residual sample at material SRV `t3`/sampler
`s3`. PRISM-conditioned environment integration samples the coefficient-only
distribution at `t6`/`s6`, adds the signed residual from `t3`/`s3` using the same
selected options, and samples the panorama at `t12`/`s12`.

`NdfParams.z` is a bitfield: bit 0 selects Quintic UV filtering, bit 1 enables
horizon fade, bit 2 enables mipmapped residual sampling, and bit 3 enables equal-angle
mapping. Quintic UV applies `6f^5 - 15f^4 + 10f^3` independently per coordinate
before the linear sample. Equal-angle mapping remaps the residual lookup radius as
`asin(r)/(pi/2)` without changing the source grid. The residual texture contains
eight levels, 128x128 through 1x1, formed by averaging signed 2x2 footprints.
When mipmapping is enabled, direct and environment residual samples use screen-space
gradients with the mipmapped anisotropic sampler (8x maximum); disabling it selects level zero. Each
residual lookup still uses one hardware texture sample. Horizon fade multiplies
only the signed residual by `smoothstep(0, 0.1, saturate(dot(N,V)))`; the coefficient
basis and normalization path are unchanged.
Material resources occupy the contiguous SRV range `t0`–`t6`; scene resources
occupy `t7`–`t12`. This separation is required by the D3D11 NVRHI backend:
binding a set applies its complete contiguous SRV range, including null holes.
If a material resource extends through a scene slot, binding the material after
the scene set clears the scene resource at that slot. Keep the two ranges
disjoint when adding bindings. Sampler ranges follow the corresponding
resource assignments.

Both color pixel-shader variants implement the same direct-light and
PRISM-conditioned IBL math. The WARP rendering tests verify coefficient and
signed-residual response, residual enable/disable parity in direct and
environment lighting, direct-light agreement at normal incidence, constant
environment IBL against an independent high-sample integral, and resource
updates after replacing the NDF or environment. `PrismNdfMathTests` also checks
the CPU basis against an independent factorial definition, double-precision
reconstruction, disk boundary/exterior behavior, signed residual, clamp, and
normalization.

The PRISM IBL loop samples the projected unit disk uniformly in area using
`r = sqrt((i + 0.5) / N)`. Its per-sample integrand is the BRDF half-vector
change of variables: `D(h) * G(v,l) * F(v,h) * (v·h) / ((n·v)(n·h))`.
Since the disk sample PDF is `1/π`, the final `π / N` multiplier is the
Monte Carlo area weight. With normalized `D`, it must not be removed or
duplicated. This derivation does not replace the required numerical comparison
against a high-sample reference.

`PrismNdfGpuParityTests.NvidiaD3D11MatchesIndependentDoubleReferenceForAllTermsAndEdgeCases`
compiles a small compute entry point that includes the production
`brdf_common.hlsli` and calls `PrismZernikeNdf` directly. It uses a hardware
NVIDIA D3D11 adapter, one-hot isolation of each of the 45 coefficients, five
interior/near-boundary/exact-boundary/exterior coordinates, a mixed-sign
45-term case, and residual/clamp/normalization cases. The independent oracle
uses double-precision factorial expansion. Every result is checked against
`1e-4 + 5e-4 * abs(reference)`. The test skips when no NVIDIA adapter is
available; WARP cannot satisfy this parity check. The production color shader
variants are compiled by the separate `CompileShaders` target.
The constant-environment WARP reference uses a 65x65 sphere render so its center
pixel has normal and view aligned. A constant RGBE panorama isolates the
specular estimator from environment variation. Its CPU oracle integrates the
same projected-disk BRDF expression at 2^20 midpoint samples; the GPU's 128
samples per pixel must agree within `2e-3 + 5e-3 * abs(reference)`. A normal
incidence directional-light result is checked independently. The test then
replaces the NDF payload and the authored environment and checks that rendered
values track the new payload and radiance, exercising material texture upload
and environment binding cache refresh.

## RTX 5080 timing budget

The opt-in
`PrismNdfGpuParityTests.NvidiaD3D11TimestampMeasuresPrismEnvironmentRenderCost`
uses D3D11 timestamp and disjoint queries on the NVIDIA adapter, warms up each
resolution, and records median GPU-timeline intervals for asynchronous final
renders. The scene contains one sphere with a generated NDF and the built-in
Studio environment; the production PRISM IBL loop uses 128 samples per covered
pixel. The 1080p acceptance budget for this workload is one 60 Hz frame
(`16.67 ms`). The interval covers render submission through its queued GPU work
and readback copy, so it is a frame-path cost rather than an isolated pixel
shader duration. Scene complexity and pixel coverage affect the result.

Observed on 2026-10-28:

| Resolution | Samples | Median GPU timeline |
| --- | ---: | ---: |
| 512x512 | 7 | 0.812448 ms |
| 1024x1024 | 5 | 2.76176 ms |
| 1920x1080 | 3 | 4.77741 ms |

Adapter and driver were identified with `nvidia-smi`: NVIDIA GeForce RTX 5080,
driver 616.64. The host was Windows 11 Pro build 26200 with an Intel Core Ultra
9 285K CPU. The Debug x64 build used MSVC 19.51.36256 and Windows SDK 10.0.26100
`fxc`; Windows power scheme was Balanced with no fixed GPU clock. The 1080p
median was below the 16.67 ms fixture budget. Background GPU load was not
controlled, and this is a single-host result, not a cross-driver guarantee or
full-scene worst-case.
Reproduce with:

```powershell
$env:ENGINE_PRISM_GPU_TIMING = '1'
out/build/x64-debug/bin/GraphicEngineUnitTests.exe `
  --gtest_filter=PrismNdfGpuParityTests.NvidiaD3D11TimestampMeasuresPrismEnvironmentRenderCost
```
