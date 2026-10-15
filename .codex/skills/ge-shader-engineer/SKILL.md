---
name: ge-shader-engineer
description: Create or modify first-party Direct3D 11 HLSL, shader build integration, or CPU/GPU data layouts owned by GraphicEngineBackends/D3D11. Use for shader and D3D11 GPU-layout tasks; use ge-cpp-coder for unrelated Core/Editor C++ work.
---

# GraphicEngine D3D11 Shader Engineer

Own first-party D3D11 shader changes and their consuming backend interface. Shared C++ quality and repository rules are in `docs/cpp-guidelines.md` and `AGENTS.md`; test procedures are in `docs/testing.md`.

## Shader workflow

1. Inspect the shader, CMake compile entry, consuming C++ code, and CPU-side structures before editing.
2. Keep HLSL under `GraphicEngineBackends/D3D11/shaders/`; do not create empty directories or assume unconfigured shader compilers are available.
3. Preserve constant-buffer alignment, packing, semantics, and bindings. Validate CPU/GPU layout agreement whenever an interface changes.
4. Compile with the configured Windows SDK `fxc` path and shader-model 5.0 profiles:

   ```powershell
   cmake --preset x64-debug
   cmake --build out/build/x64-debug --target CompileShaders
   ```

5. For associated backend C++ or runtime behavior changes, use `docs/cpp-guidelines.md` and `docs/testing.md` for applicable checks.

## Handoff

Report changed shader interfaces and files, CPU/GPU layout validation, shader compile results, related tests, and remaining risks.
