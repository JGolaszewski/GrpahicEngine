# Graphics Engine Re-Audit Report V2

**Audit date:** 2026-10-06  
**Role:** Lead Engine Architect / strict code review  
**Scope:** Project-owned engine core, render submission, render graph, GPU resource lifetime, D3D11/UI boundary, asset/material path, scene data, and editor coordination. Vendored NVRHI, ImGui, GLFW, and other dependencies were not audited line-by-line.  
**Method:** Git history and implementation diffs, then static inspection of the current source. The final Visual Studio x64 Debug build was run through the Visual Studio Developer Command Prompt. No performance capture or GPU runtime validation was available.

## Executive Summary

**Implementation update (2026-10-06):** All five P0 actions and all six P1 actions below are implemented and committed. P2 glTF material coverage reporting, configurable demand-driven shadow slices, cached bounds and submission timings, and the runtime/render/backend/UI target split are also implemented and committed. The Visual Studio x64 Debug build succeeds after these changes. CTest passed 2/2 after P0; it was not rerun after later changes. Interactive GPU validation and performance captures have not been run in this session.

The implementation phase resolved several real problems from `ENGINE_AUDIT_REPORT.md`: lights are now prepared per view, imported alpha mode and sidedness reach forward draw state, ordinary resource retirement no longer calls `waitForIdle()`, editor ImGui/D3D11 integration moved out of the core target, scene ID lookups gained indexes, and multiple placeholder or misleading APIs were removed. The fixes are incremental and generally fit the existing design.

The completion markers in the original report overstate the degree of closure. Some changes are foundations rather than complete systems. The follow-up P0 fixes closed the two material correctness gaps found in this review:

1. Per-view light uploads now use a separate volatile buffer per submitted view slot, so the view count is not constrained by one buffer's version budget.
2. Masked shadow casters use a depth-only alpha-test shader and material bindings. Shadow culling respects double-sided materials; blended materials are excluded from shadow casting by documented policy.

The P1 follow-up now puts reusable camera, light, and object upload buffers under `FrameContext`; caches per-view scene binding sets; imports viewport attachments and declares their states, ranges, and clear/store policy in the graph; validates subresource hazards; batches queued asset uploads with completion-based staging retirement; and routes editor material and scene-document workflows through narrower APIs. The P2 follow-up documents and rejects unsupported glTF material features, adds configurable demand-driven shadow slices and bounds/timing instrumentation, separates runtime/render/backend/UI build targets, and routes viewport texture registration through the backend contract. The graph remains texture-focused, and ImGui frame rendering remains D3D11-specific.

**Review disposition:** P0 and P1 implementation is complete. Applicable unconditional P2 implementation work is also complete and builds with the Visual Studio x64 Debug toolchain. Conditional P2 work remains deferred where representative performance captures or a target presentation requirement are absent. Interactive GPU validation remains a documented manual step.

## 1. Commit Verification and Diff Review

The original report was introduced in `730ae10` after the P0 implementation commits. `37f35aa` and `81d5360` update its status labels; they do not add implementation. The corresponding implementation commits were reviewed directly rather than treating those labels as evidence.

| Finding from original report | Implementation commit(s) | Review result |
|---|---|---|
| P0 per-view lighting/shadows | `d05547b`, `e0cd3b5` | Per-view light buffers remove the fixed view-count ceiling. A four-view distinct-light scenario is documented in `docs/validation/windows.md`; GPU execution remains manual. |
| P0 alpha mode and sidedness | `2ddb8b6`, `7848385` | Forward and shadow paths honor mask alpha and double-sided state. Blended materials do not cast shadows by policy. |
| P1 frame and per-view allocation churn | `44d83e7`, `aa3f9c0`, `6ccf7e6` | `FrameContext` owns persistent camera/light/object upload buffers and view lifetime; scene binding sets are cached by resource handles. Per-object data still uses one write per draw and sizes volatile versions from an estimate. |
| P1 device-wide idle during retirement | `a3e6a0f`, `f15b51a` | Normal retirement polls graphics event queries. Query allocation failure is diagnosed and falls back to an explicit idle wait before releasing retained resources; teardown also waits idle. |
| P1 ImGui/D3D11 coupling | `64f94cb`, `2bdfdc4` | Core no longer owns ImGui frame lifecycle or links D3D11/GLFW/ImGui. The P2 follow-up adds a backend-mediated opaque viewport texture registration contract and an `EditorUI` target; ImGui frame rendering remains D3D11-specific. |
| P1 render graph access states | `4b06573`, `2dc188f`, `fa207d1`, `79e7ae6` | Viewport color/depth attachments are imported with clear/store policy; shadow and viewport accesses use subresource ranges; overlapping declarations and subresource read/write hazards are checked. `RenderGraph::Dump()` exposes passes, resources, lifetimes, and planned transitions. Graph resources remain texture-only. |
| P1 upload submission | `e4294d7` | Mesh and texture writes queue into one upload command list per flush. CPU staging is retained through a GPU completion query, with byte and batch progress plus error reporting. |
| P1 editor/runtime service boundary | `d032dcc` | Editor material operations use focused `RenderSystem` methods, renderer implementation getters were removed, and `EditorSceneDocument` reads/writes `SceneDocument` data through `AssetManager` without depending on `Application`. |
| P2 scene document workflow | `8ae35df` | Path and canonicalization state moved into `EditorSceneDocument`. Load/save still call `Application` methods and the service is editor-specific; serialization ownership is only partially separated. |
| P2 scene ID scans | `ad7fc02` | ID-to-index lookups are now indexed for entities, lights, and cameras. Removal still shifts vectors and rewrites following indexes, and pointer-to-ID lookup remains linear. This is a useful improvement without stable slot/generation handles. |
| P2 glTF material coverage | `39b37e9` | Unsupported material channels and states are rejected with source context; `GraphicEngineCore/docs/gltf-material-support.md` records supported and rejected fields. |
| P2 shadow policy and slice allocation | `7dd3818`, `f72182e` | Shadow resolution and per-scene caster limits are configurable. The shadow array is sized to active caster views, with one fallback slice when no views exist. |
| P2 bounds and submission timing | `ce32ab9` | Per-submesh world bounds are cached by effective transform and local bounds; culling and packet-build CPU time are exposed in submission statistics. |
| P2 module targets and texture registration | `e3c70a7` | Runtime, render core, D3D11 backend, and editor UI have separate CMake targets. Viewport texture registration now crosses an opaque backend contract; runtime/render objects preserve the existing core DLL ABI. |
| P3 placeholder/API cleanup | `a9e4515`, `369e3ea`, `91ff71e`, `64a314d`, `d0c569a` | Empty cache declarations were removed; sandbox primitive construction uses the factory; `EntityBuilder` is private to implementation; unsupported viewport flags were deleted; unsupported neural BRDF values are explicitly downgraded with a warning. These are scoped cleanups. |

Related history includes model/material texture import and reimport (`4fc82a4` through `8dd9597`), per-submesh shader selection (`c03c7ab`), and editor file-dialog isolation (`6c31522`). Unsupported glTF material features are now rejected with a checked-in coverage table.

### 1.1 Representative patterns in the diffs

The multi-scene change replaces one global light list with view-owned data, which is the correct ownership model:

```cpp
// Current direction in RenderSystem::SubmitViews
for (Renderer::RenderView* view : bb.GetSceneViews()) {
    if (view && view->TargetScene)
        m_SceneRenderer->BuildLighting(view->TargetScene, bb.GetShadowViews(), view->Lights);
}
```

The follow-up should make the GPU upload lifetime match that per-view ownership. Current code instead rewrites one three-version buffer as each view is drawn:

```cpp
// SceneRenderer::Init
lightCbDesc.isVolatile = true;
lightCbDesc.maxVersions = 3;

// ForwardPass::Execute, once for every scene view
cmd->writeBuffer(lightBuffer, &lightData, sizeof(lightData));
```

Use an explicit frame/view allocation or size the upload capacity from the maximum number of view writes recorded in the frame. A constant of three does not follow from the API’s arbitrary view count.

Forward alpha classification is now represented, but the shadow branch intentionally strips the very state needed for alpha-tested shadows:

```cpp
// SceneRenderer::CullAndFillView
packet.Queue = view->ViewType == RenderView::Type::Shadow
    ? RenderQueueType::Opaque : classifyFromMaterialAlphaMode(...);

// Shadow pipeline setup
key.PS = nullptr;
key.SceneLayout = nullptr;
key.MaterialLayout = nullptr;
key.CullMode = nvrhi::RasterCullMode::Back;
```

Masked shadow casters need an alpha-aware depth shader and base-color texture/material binding. Their shadow culling policy should also preserve the material’s two-sided intent.

## 2. Current Codebase Audit

### 2.1 Core architecture and API unification

**Improved:** `GraphicEngineCore` no longer links the concrete D3D11 backend, GLFW, or ImGui. `Application::Run` receives an `IWindow` and `IRenderBackend`, and the editor owns ImGui initialization and shutdown. The D3D11 platform target is now separate.

**Still coupled:**

- `GraphicEngineCore` publicly links NVRHI and DirectXMath, and its public headers expose NVRHI types extensively. Runtime and render sources now compile as separate `EngineRuntime` and `RenderCore` object targets while composing into the existing core DLL for ABI compatibility.
- Editor material creation, texture assignment, and cleanup use focused `RenderSystem` methods. Asset operations still pass through the application-owned `AssetManager` and render-system asset APIs.
- `EditorUiIntegration` is now part of its own `EditorUI` target. Viewport texture registration crosses the backend through an opaque-handle contract, while current ImGui frame rendering remains D3D11/GLFW-specific.
- `EditorSceneDocument` owns path state and reads/writes the serializable `SceneDocument` through `AssetManager`. Runtime scene construction remains an `Application` operation, separate from file persistence.
- Naming, member casing, and getter/mutable-field conventions remain mixed. Treat this as secondary to ownership and lifecycle contracts; avoid a broad style-only rewrite.

**Assessment:** target boundaries now distinguish `EngineRuntime`, `RenderCore`, `RenderBackendD3D11`, and `EditorUI`; the first two compose the existing `GraphicEngineCore` DLL. The current editor UI frame renderer remains tied to the D3D11/GLFW/ImGui path, while viewport texture registration is routed through the backend contract.

### 2.2 Render pass health and graph maturity

The active graph remains three passes: shadow, forward, debug. It does not yet own all render work.

- `ShadowPass` creates framebuffers from the physical shadow texture and clears each submitted shadow slice. Slice count now follows eligible casters and configured per-scene limits.
- `ForwardPass` reuses scene binding sets and consumes graph-imported viewport attachments. The graph declares color/depth accesses, clear/load/store intent, and final states; `DebugPass` declares its attachment writes.
- Pass execution returns an error result. `RenderSystem::EndFrame` logs graph/execute failures and clears submitted targets to the error color before submission.
- The graph tracks access hazards per texture subresource and exposes pass, resource, lifetime, and transition dumps. It still has no buffer resources or queue ownership, and first/last allocation lifetime remains resource-wide.
- Forward geometry, material evaluation, lighting, and shadow sampling still live together in one pass/shader path. No HDR/tone-map or post-process stage is present. This can be a valid small forward renderer; the graph should not be described as a general frame scheduler yet.

The graph’s new declarations are a positive move toward a strict pass contract. They should be expanded with validation and execution ownership before compute, async queues, or more complex post-process work is layered on top.

### 2.3 GPU lifetime, uploads, and per-frame data

**Deferred retirement:** `ResourceManager::AdvanceSubmittedFrame` places an event query after graphics work, polls in-flight frames, and releases retired meshes/textures only after `m_CompletedFrames` reaches the retirement frame. This is materially safer than freeing on CPU-frame age or stalling on every retirement. The destructor’s `waitForIdle()` is teardown-only.

**Remaining risks and churn:**

- If `createEventQuery()` fails, the resource manager logs the failure, waits for GPU idle on that exceptional path, and clears resources whose use is then complete. Successful ordinary retirement remains query-based and does not wait device-wide.
- `LightsCBData` uses a dedicated buffer for each view slot. Each slot is written once per frame and its three volatile versions cover reuse across the supported frames in flight.
- `FrameContext` owns the view wrappers and persistent camera, light, and object upload buffers. Scene binding sets are reused while their underlying handles remain stable. The blackboard keeps non-owning view references whose lifetime is bounded by the owning frame context.
- Per-object data now avoids one GPU buffer and one binding set per draw, but still does one `writeBuffer` per draw. The volatile version count is based on an estimated worst-case draw count and can allocate a large number of versions for scenes with many visible candidates. Profile memory and CPU submission cost before considering it a finished frame allocator.
- `RenderDevice::CreateMesh` and `CreateTexture` allocate resources; `ResourceManager` queues their writes and submits one upload command list per flush. Staging data remains alive until a completion query retires it. Upload progress reports queued/submitted/completed bytes and failed batches.
- Upload recording and graphics execution use the graphics queue. CPU import is still synchronous; a background import worker and asynchronous upload queue remain future work.

### 2.4 Materials, asset import, and rendering behavior

Forward rendering now classifies opaque/masked/blended materials, applies mask cutoff, enables alpha blending with depth writes disabled for blends, and disables face culling for double-sided materials. Material ABI v2 is size-asserted and scene serialization preserves the new properties. These changes close important parts of the original defect.

The contract is not yet end-to-end:

- Masked alpha is evaluated in the shadow map. Blended materials intentionally do not cast shadows because the depth-only representation does not model partial coverage.
- Only base-color and normal maps are bound in the current material system. The importer rejects unsupported metallic-roughness texture channels, occlusion, emissive texture, non-default normal scale, alternate UV sets, and sampler state with source context. The supported/rejected field table is in `GraphicEngineCore/docs/gltf-material-support.md`.
- Alpha blend sorting uses view-space center depth and descending order, which is a sensible baseline. It does not split intersecting geometry or handle order-independent transparency; keep its scope documented and validate representative scenes.
- Shadow generation remains positional/spot-light only. Resolution and per-scene caster limits are configurable, and the graph allocates slices only for eligible casters. Directional cascades remain deferred until representative scene content warrants them.
- World-space submesh bounds are cached by effective transform and local bounds, and submission statistics measure culling and packet-build CPU time. No performance capture was available, so batching/instancing remains deferred until timings identify draw submission as a bottleneck.

### 2.5 Scene data and editor health

Persistent IDs now have hash indexes for direct ID lookup. The store is still parallel vectors plus ID arrays and index maps; deletion shifts entries and rewrites map indices. This is adequate for current scene sizes, but not the end state for stable handles, component storage, transform hierarchy, or parallel render extraction.

Scene file parsing remains versioned and validates values before replacing the runtime scene. The extracted editor document object is currently a path wrapper rather than the source of the serialization workflow. Keep runtime scene DTO conversion and disk I/O independent from editor UI state as the schema grows.

The editor has a separate executable, panels, commands, and file-dialog/UI integration files, which are healthy seams. `Editor::Editor.cpp` still coordinates scene lifecycle, asset import/reimport, selection, camera/input, undo/redo, viewport management, panel event dispatch, and direct render-system calls. Editor actions are not yet cleanly routed through editor services; panels/events and command history do not make the coordinator small or independently testable by themselves.

## 3. Prioritized Next-Stage Roadmap

### P0 — Close correctness regressions and establish repeatable validation

1. **[Completed] Fix per-view light upload capacity.** Each submitted view slot owns a separate light buffer. The four-view distinct-scene/light regression scenario is documented in `docs/validation/windows.md`.
2. **[Completed] Add alpha-aware shadow rendering.** Masked materials sample base-color alpha and apply their cutoff; double-sided shadow culling is preserved. Blended materials are documented as non-casters.
3. **[Completed] Make frame failure handling deterministic.** Pass results propagate through the graph; compile or execution failure clears submitted viewports to a known error color before submission.
4. **[Completed] Validate resource retirement failure modes.** Event-query creation failure is logged and takes an explicit `waitForIdle()` recovery path before releasing deferred resources. Ordinary successful frame retirement remains query-based. GPU-load reimport/release checks are documented for manual validation.
5. **[Completed] Restore an executable verification path.** `scripts/verify-windows.ps1` configures, builds, and runs CTest under Visual Studio. The Debug build and both registered tests pass. Interactive GPU scenarios remain manual and were not run in this session.

**Implementation exit criteria: Completed.** The build/tests pass and manual GPU validation steps are documented for multi-view lighting, alpha shadows, failure clears, and resource lifetime. Runtime visual and GPU-load results remain unverified until those scenarios are run.

### P1 — Make the frame and graph contracts real

1. **[Completed] Add a frame context.** `FrameContext` owns frame timeline/query state, views, transient resource lifetime, upload buffers, and per-frame diagnostics. Blackboard view references are non-owning and bounded by this owner.
2. **[Completed] Remove remaining per-view object churn.** Camera buffers and scene binding sets are reused by view slot and stable resource handles; frame counters track allocations, uploads, and draws.
3. **[Completed] Finish graph ownership for active passes.** Viewport color/depth resources are imported with explicit states and attachment policy; viewport and shadow accesses declare subresource ranges.
4. **[Completed] Add graph validation and diagnostics.** Subresource hazards, capabilities, and conflicting declarations are checked; `Dump()` exposes pass/resource/lifetime/transition data; pass execution errors propagate.
5. **[Completed] Batch GPU uploads.** Mesh and texture writes share queued command-list flushes, progress/error reporting, and completion-based staging retirement.
6. **[Completed] Create narrow editor/runtime services.** Editor material operations use `RenderSystem` service methods, while `EditorSceneDocument` persists DTOs through `AssetManager` without depending on `Application`.

**Implementation exit criteria: Completed.** Active render target states and transitions are graph-managed; camera/light/object buffers and bindings are reused; asset uploads batch and retire staging by completion; editor file persistence and material calls cross focused APIs. GPU visual behavior remains unverified until the documented scenarios are run.

### P2 — Improve scalability and imported-material fidelity

1. **[Completed]** Report glTF material support with a coverage table and reject unsupported metallic-roughness, occlusion, emissive, normal-scale, UV-selection, and sampler features at import with source context. See `GraphicEngineCore/docs/gltf-material-support.md`.
2. **[Completed; cascades deferred]** Expose shadow resolution and per-scene caster limits and allocate only slices for eligible shadow-casting lights. Directional cascades remain deferred until representative scene content warrants them.
3. **[Completed; batching deferred]** Cache per-submesh world bounds and measure culling and packet-build CPU time. No capture establishes draw submission as a bottleneck, so batching/instancing is not justified yet.
4. **[Deferred]** Keep the current indexed scene storage until scene growth or mutation patterns justify stable generational handles. Current audit evidence does not establish that need.
5. **[Completed]** Separate `EngineRuntime`, `RenderCore`, `RenderBackendD3D11`, and `EditorUI` build targets with private module dependencies and an opaque backend viewport-texture registration contract. Runtime and render object targets compose into `GraphicEngineCore` for ABI compatibility.
6. **[Deferred]** Add output color-space/HDR policy and post-processing when a target presentation requirement and reference captures are available; none were provided in this audit.

### P3 — Consistency and long-term maintainability

1. Adopt a project C++ formatting/naming and error/result convention across core and editor, enforced in both targets.
2. Reduce broad public headers and transitive type exposure; compile public headers independently.
3. Consolidate editor requests, commands, notifications, and OS input into separately named interfaces with clear ownership and subscription lifetime.
4. Add a second graphics backend only after device/swapchain/presentation/shader/lifetime contracts are demonstrably backend-neutral. A second backend should be validated with a clear-and-triangle conformance sample before a full renderer port.

## 4. Verification Record and Limitations

- `git status --short` showed a pre-existing user change to `.gitignore`; it was preserved. This re-audit did not edit it.
- Git commit history and the implementation diffs listed above were inspected. The report status-only commits were not mistaken for implementation.
- The Visual Studio 2026 x64 Developer Command Prompt configured the existing CMake build. `cmake --build out/build/verify-p0 --config Debug --parallel 4` succeeded after the P1 changes.
- The same Visual Studio x64 Debug build succeeded after glTF rejection/coverage reporting, shadow policy controls, bounds/timing instrumentation, and the CMake target split.
- CTest passed both registered tests after P0; it was not rerun after P1. No interactive GPU validation or performance capture was run.

## Final Assessment

P0 and P1 implementation is complete, and applicable unconditional P2 items are implemented. Unsupported glTF features are explicitly rejected and documented. Shadow controls and demand-driven slices, cached bounds/timing instrumentation, separate build targets, and backend-mediated viewport texture registration are in place. The Visual Studio x64 Debug build succeeds. The P0 CTest run passed 2/2; tests were not rerun after later changes. Interactive GPU validation remains outstanding. Conditional P2 work (directional cascades, batching, generational handles, HDR/post-processing) remains deferred pending representative content, performance evidence, or presentation requirements.
