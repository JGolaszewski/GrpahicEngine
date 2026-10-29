# PRISM model runtime and release package

Windows application releases are intended to contain the complete application
and PRISM runtime in one `GraphicEngine-<tag>-windows-x64.zip` archive. The
archive includes the editor and engine binaries, hash-pinned PRISM ONNX graphs
and external weight files, embedded CPython for tokenization, the pinned
`tokenizers` wheel contents, the required vcpkg runtime DLLs (including ONNX
Runtime and OpenImageIO), notices, and a SHA-256 sidecar. Model files and
Python runtime binaries are generated during release packaging and are never
committed to Git.

## Development and ordinary builds

Ordinary configure/build presets do not enable `prism-inference`, download
models, or require Python or ONNX Runtime. Saved generated materials remain
renderable from their serialized material data. Only the dedicated
`x64-release-prism` preset enables the optional vcpkg ONNX Runtime dependency.
The staged model files remain in `out/prism-runtime` during development; CMake
install places them beside the editor for a relocatable application bundle.

## Run the build-tree editor

On Windows, stage the models and embedded tokenizer runtime once, configure the
PRISM preset, then build its run target:

```powershell
python scripts/download_prism_models.py
python scripts/stage_prism_runtime.py
python scripts/validate_prism_onnx_contract.py
cmake --preset x64-release-prism
cmake --build --preset x64-release-prism --target run-editor-prism
```

`run-editor-prism` launches the build-tree editor with
`ENGINE_PRISM_RUNTIME_ROOT` supplied as `GRAPHICENGINE_PRISM_RUNTIME_DIR`. It
does not copy the large model files into the build output. Launching
`out/build/x64-release-prism/bin/Editor.exe` directly also resolves the staged
root recorded by the PRISM-enabled configure. An explicit
`GRAPHICENGINE_PRISM_RUNTIME_DIR` environment variable overrides both paths.

## Create and verify an installable package

For a local package staging run on 64-bit Windows:

```powershell
python scripts/download_prism_models.py
python scripts/stage_prism_runtime.py
python scripts/validate_prism_onnx_contract.py
cmake --preset x64-release-prism
cmake --build --preset x64-release-prism
cmake --install out/build/x64-release-prism --prefix out/package-prism --config Release
python scripts/verify_prism_package.py --package out/package-prism
$env:PATH = "$(Resolve-Path out/package-prism/bin);$env:PATH"
Copy-Item out/build/x64-release-prism/bin/PrismNdfPackageSmoke.exe out/package-prism/bin/
Push-Location out/package-prism/bin
try {
    .\PrismNdfPackageSmoke.exe .
} finally {
    Pop-Location
}
Move-Item out/package-prism/bin/PrismNdfPackageSmoke.exe out/prism-smoke/ -Force
```

Launch `out/package-prism/bin/Editor.exe` to use the installed, relocatable
runtime layout. A build alone does not install the package files; use the
`run-editor-prism` target for development or `cmake --install` for a package.

Downloads land under the ignored `out/prism-runtime/` directory. Both model
assets and the embedded Python and tokenizer archives are checked against
expected byte counts and SHA-256 values before use. Staging builds a clean
sibling tree and swaps it into place only after validation; verified existing
model files may be reused, while stale files are removed from the managed
model directory. If staging fails, the previously installed directory remains
available. The pinned decoder emits
only `ndf_grid_flat` (`float32[1,16384]`); it does not emit Zernike
coefficients. The release validator requires this output name, dtype, and
shape and rejects unexpected decoder outputs. After native C++ ONNX inference,
the material generation service must fit the returned 128×128 grid to the
45-term degree-8 Zernike basis using least squares, then retain the signed
per-sample residual `grid - fitted_basis`. Rendering evaluates the fitted
polynomial and adds the residual, preserving the decoded grid at its samples.
The fit and residual construction belong to the C++ generation/postprocessing
path, not to the ONNX model or release validator.

## Runtime boundary

The embedded CPython 3.13.17 runtime runs a persistent JSON-lines worker that
prefixes prompts with `query:` followed by a space and emits exactly 128 `input_ids` and
`attention_mask` values. The 128×128 predictor consumes seven numeric controls
for roughness, anisotropy, and axial orientation. If directional mode is enabled
and the prompt omits anisotropy, native inference supplies maximum normalized
anisotropy while preserving any authored numeric controls. Python performs no
model inference. The bundle supplies ONNX Runtime CPU libraries for the
native C++ E5, text predictor, and decoder inference service. The package smoke
executable runs a real directional prompt through that service and checks the
returned prompt, provenance, finite coefficients and residuals, and positive
normalization. The worker's startup handshake and per-request errors use JSON
lines on stdout; diagnostic output belongs on stderr.

## Pins, provenance, and licenses

The exact upstream PRISM source commit, E5 model revision, release tags, file
sizes, and SHA-256 digests are recorded in
`scripts/prism-model-assets.json`. The embedded CPython archive and tokenizer
wheel pins are in `scripts/prism-runtime-assets.json`. ONNX
Runtime is an optional vcpkg manifest feature resolved through the repository's
pinned builtin baseline. No unpinned `latest` URL is used.

The release workflow verifies the installed model files, ONNX tensor
contracts, package notices, embedded Python/tokenizers versions, tokenizer
worker protocol and fixed tensor lengths, and ONNX Runtime DLL presence. It
then runs native inference against the installed package before creating the
release ZIP. It also runs the ZIP integrity check and publishes a SHA-256
sidecar for the completed archive. The embedded runtime carries a per-file size
and SHA-256 inventory, which is checked both after staging and against the
installed package.

E5 model/tokenizer licensing and PRISM-trained model licensing have distinct
terms. The packaged notices carry the E5 MIT and PRISM CC BY-SA 4.0 texts and
attributions. CPython and `tokenizers` retain their upstream license notices in
the installed runtime. These notices travel with the application bundle.
