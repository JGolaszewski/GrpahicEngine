# P0 implementation report

## Scope and branch

The recommendation report identifies F-01 through F-06 as the highest-priority work: the P0 correctness and stability blockers in Etap 0. This branch was created from `MaterialSystem` at `611bd2d`:

- Branch: `codex-p0-correctness-fixes`
- Scope: F-01–F-06 only
- Excluded: P1/P2 fixes and new features, because the source report explicitly places them after the renderer core is stable

The pre-existing worktree edits in `CMakeLists.txt`, `SandboxApp/main.cpp`, and the two earlier reports were preserved. They were not folded into the P0 commits unless a file needed to be changed for a P0 fix.

## Changes and decisions

### F-01 — CPU/GPU light-buffer layout

Commit: `45fb36a` — `Fix CPU and GPU light buffer layout`

Implemented:

- Added the `ENGINE_MAX_LIGHTS` CMake setting, passed consistently to C++ and `fxc`.
- Changed the HLSL light array to `g_Lights[ENGINE_MAX_LIGHTS]`, which is eight entries in the current configuration.
- Moved `LightsCBData` into the shared renderer light header so `SceneRenderer` and `ForwardPass` use the same CPU definition.
- Added size and offset `static_assert`s for `DirectionalLightData` and `LightsCBData`.
- Clamped scene light collection to the supported limit and zero-initialized the upload structure before writing it.

Decision: the limit is controlled by the build configuration rather than maintaining independent C++ and HLSL constants. The HLSL fallback remains eight for direct shader-tool use, while normal builds receive the configured value from CMake.

### F-02 — command-list ownership

Commit: `2f0dbd3` — `Centralize immediate command list submission`

Implemented:

- Removed the internal `executeCommandList` call from `EntityBuilder::Build()`.
- Kept mesh upload and entity building record-only.
- Left `RenderSystem::ExecuteImmediate()` as the single open/close/execute wrapper for immediate uploads.

Decision: this preserves batching. Creating multiple entities through one immediate callback now records all uploads into one list and submits it once. The renderer’s separate scene-upload list remains independent because it is a persistent frame upload path, not an entity-builder submission.

### F-03 — material lifecycle and constant-buffer upload

Commit: `49c71ef` — `Fix material dirty state and initial uploads`

Implemented:

- All material mutators now set dirty state only when the effective value changes.
- `RenderSystem::BeginFrame()` uploads dirty materials on its already-open frame command list, including initial constant-buffer contents.
- Added `MaterialSystem::CreateMaterial()` to allocate an ID, register the material, and initialize its properties as one operation.
- Updated the Editor add-object path to create a material from the requested color, emissive, metallic, and roughness values and pass it to `EntityBuilder`.
- Made `EntityBuilder::Build()` fail safely when no device, command list, shape, or material is supplied.
- Simplified Inspector material changes to update CPU properties; the next frame performs the GPU upload.

Decision: a missing material is rejected rather than silently creating an unregistered fallback. The builder does not own a `MaterialSystem`, so a fallback would obscure ownership and could still lack a GPU binding. The factory is the preferred safe creation path for clients.

### F-04 — view and geometry submission

Commit: `6b7a659` — `Submit default views and seed editor scene`

Implemented:

- Added an application fallback that binds the first active scene and its first camera to a viewport wrapping the default framebuffer when no explicit view was registered.
- Recreated that default viewport after swap-chain resize.
- Added a real, factory-backed demo cube to the Editor scene.
- Kept explicit Editor viewport registration higher priority than the fallback.

Decision: the fallback is centralized in `Application` so Sandbox and other minimal applications do not need duplicate boilerplate. Explicit views remain authoritative, which preserves the Editor’s off-screen viewport behavior while both applications use the same `SubmitViews` path.

### F-05 — shadow-map SRV/DSV hazard

Commit: `dac9f61` — `Separate shadow pass resource bindings`

Implemented:

- Marked the render-graph shadow texture as shader-readable.
- Removed scene/light/shadow SRV bindings from `ShadowPass`.
- Made shadow pipelines use only the per-object binding layout; `PipelineCache` now supports optional scene and material layouts.
- Kept shadow-map SRV binding exclusively in `ForwardPass`.
- Invalidated cached shadow framebuffers when the physical render-graph texture changes.

Decision: the existing shadow vertex shader only consumes per-object data, so a separate shadow-specific binding layout was unnecessary; omitting scene/material layouts entirely gives the smallest valid layout and guarantees the shadow texture is not simultaneously bound as SRV and DSV.

### F-06 — ImGui frame and shutdown

Commit: `92a334c` — `Fix ImGui frame and shutdown lifecycle`

Implemented:

- Removed the second `ImGui::Render()` from the DX11 backend.
- Kept frame finalization in `Application`, with the backend only consuming `ImGui::GetDrawData()`.
- Added explicit shutdown methods to the window and render-backend interfaces and implementations.
- Added orderly shutdown: DX11 backend, GLFW backend, ImGui context, render system, renderer backend, then window.
- Enabled D3D11 debug-layer and NVRHI validation support in debug builds, with a retry without the optional D3D11 debug layer when it is not installed.

Decision: shutdown is centralized at the application boundary because it can enforce ordering across both platform backends and the ImGui context. The debug-layer fallback keeps developer diagnostics opportunistic without making startup depend on an optional SDK component.

## Verification

- The Visual Studio developer-environment Debug build passed after each P0 change.
- Final build command: `cmake --build out/build/x64-debug --config Debug -j 4`
- Exactly one `ImGui::Render()` remains, in `Application.cpp`.
- No `g_Lights[128]` declaration remains; the shader and CPU upload path use `ENGINE_MAX_LIGHTS`.
- The duplicate entity-builder command-list execution is gone.
- `git diff --check` reports no whitespace errors.
- A five-second Editor smoke test completed without an access violation and reported zero NVRHI validation errors.

## Runtime follow-up

Commit: `eca3932` — `Fix DX11 command-list lifetime and frame uploads`

The reported `RenderSystem.cpp:34` access violation was caused by storing a raw `ICommandList*` from a temporary `CommandListHandle`. The handle released the command list at the end of the initializer, leaving a dangling, non-null pointer. `RenderSystem` now owns the handle and rejects renderer initialization when creation fails.

The smoke test then exposed two related DX11 validation issues:

- `SceneRenderer` was opening a second immediate command list while the frame list was open; per-object uploads now use the frame list supplied by `RenderSystem`.
- Per-object data used a volatile constant buffer with subrange bindings; it is now a regular constant buffer with 256-byte binding ranges, matching DX11/NVRHI requirements.

`RenderView` matrices and frustum planes are initialized, and clearing a view no longer discards its target scene before culling.

## Result

The six P0 changes and the runtime follow-up are complete. The follow-up is kept in its own commit, and this report remains a separate documentation change so the implementation history and decisions stay auditable.
