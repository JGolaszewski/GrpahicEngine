+# Graphics Engine and Editor Architecture Audit

**Audit date:** 2026-10-06  
**Scope:** Project-owned C++ engine core, rendering path, asset/scene systems, shaders, and editor. Vendored dependencies were treated as dependencies, not audited line-by-line.  
**Evidence basis:** Static inspection of repository sources and build configuration. No runtime profiling or build/test execution was performed for this audit. Recommendations that depend on measured performance are labeled as such.

## Executive Summary

This is a Windows-first C++23 editor and rendering prototype with a useful foundation: the engine uses NVRHI handles above D3D11, owns a pipeline cache, has a working asset import/cache path, includes a render-graph skeleton, and already separates editor panels from the application class at the file level. Scene data is serialized with validation and atomic replacement; editor edits are increasingly represented as undoable commands.

The main architectural issue is that the abstractions are not yet the actual boundaries of the system. The core target owns ImGui lifecycle and links ImGui's D3D11 backend, the renderer and viewport presentation path use raw D3D11 types, the render graph only wraps three existing passes, and frame submission is still a synchronous immediate-context style pipeline. The editor executable is logically a consumer of the engine but several engine abstractions are editor-shaped.

The most urgent correctness and scale risks found in source are:

1. **One light/shadow set is built from only the first submitted view's scene.** RenderSystem::SubmitViews retains the first valid TargetScene as activeScene, then builds shared lighting and shadow data once. If callers submit views of distinct scenes, the other views render their geometry with the first scene's light data.
2. **Draw submission does not use the material's alpha mode or render queue.** Imported alpha/sidedness data is warned about as unsupported, all visible scene submeshes are currently queued as opaque, and queue sorting groups by pipeline/material/buffer even for the transparent queue. Correct blending and depth ordering are not implemented.
3. **Per-object GPU data and binding sets are allocated per visible object per view per frame.** This creates avoidable driver/API object churn and prevents an efficient multi-threaded recording path.
4. **GPU resource retirement can call waitForIdle() during normal operation.** The frame-delay queue is a useful start, but on a retirement boundary it waits for the whole device, creating a likely hitch. Use fences/timeline values and retire by completed GPU work.
5. **The renderer's resource state model is too small for a general graph.** It tracks only texture read/write at whole-resource granularity and infers state from a boolean write flag. There are no buffer resources, subresource ranges, explicit access declarations, queue ownership, or compute passes yet.
6. **Core, backend, and editor responsibilities are tangled.** The engine core initializes ImGui and exposes ImGui frame methods on IRenderBackend; ViewportTarget acquires a D3D11 SRV to hand to ImGui; CMake links ImGui into the core. This makes a second graphics backend or headless engine difficult even though NVRHI is present.

### Priority summary

| Priority | Finding | User-visible consequence |
|---|---|---|
| P0 — Completed (`d05547b`) | Per-view scene lighting is global to the first view | Incorrect lighting/shadows when rendering multiple scenes in one frame |
| P0 — Completed (`2ddb8b6`) | Alpha/transparency and material sidedness do not reach draw state | Imported assets can render incorrectly; transparent sorting is unsafe |
| P1 | GPU allocations per object per view and frame | CPU overhead and a poor path to larger scenes or parallel submission |
| P1 | Device-wide idle wait during deferred resource retirement | Periodic frame-time spikes while assets are reimported or removed |
| P1 | Renderer, viewport, and ImGui are D3D11-coupled | Backend portability and testing are blocked |
| P1 | Render graph is partial and declarations are weak | Pass dependencies, transitions, and resource aliasing are not yet reliable enough to grow safely |
| P2 | Editor orchestration and application lifecycle are large and tightly coupled | Slower changes, harder editor automation, weaker unit-test seams |
| P2 | Scene is a flat shared-pointer collection with linear ID lookup | Scaling, hierarchy, transform propagation, and serialization evolution need redesign |
| P3 | Inconsistent conventions and unfinished APIs | Higher maintenance cost and unclear ownership |

## 1. Current State Assessment

### 1.1 Architecture map

The current compile-time and runtime structure is approximately:

```text
SandboxApp / GraphicEngineEditor
             |
             v
      Engine::Application
      (window loop, scenes, views,
       ImGui context/frame lifecycle)
             |
     +-------+---------+
     |                 |
 AssetManager      RenderSystem
 (import, registry,  (frame orchestration,
  scene documents)   resources, render graph)
                       |
             +---------+---------+
             |                   |
         SceneRenderer       RenderGraph
         (cull, packetize,    (ShadowPass,
          lighting/shadows)    ForwardPass,
                               DebugPass)
             |                   |
             +---------+---------+
                       |
                 NVRHI handles
                       |
             D3D11Backend / DXGI
                       |
                  D3D11 device

GraphicEngineEditor:
  Editor : Engine::Application
    - editor state, selection, scene actions,
      asset actions, event routing, undo/redo
    - panel collection
      - Hierarchy / Inspector / Scene / MainBar /
        Statistics / Asset Browser
    - synchronous panel request events
    - command history and command implementations
```

The apparent renderer abstraction (IRenderBackend and NVRHI) is partial. D3D11Backend creates the NVRHI device, but the render backend interface also owns ImGui lifecycle. ViewportTarget gets a native ID3D11ShaderResourceView, and the editor displays that pointer via the D3D11 ImGui backend. Shader compilation uses fxc to emit DXBC. These are concrete D3D11 assumptions distributed across target boundaries.

### 1.2 Engine and runtime

- Engine::Application owns the main loop, window, backend, ImGui context, renderer, active scene/view collections, and convenience operations for scene load/save. Its lifecycle is a usable small-app base, but it combines platform, UI, scene, and renderer policy.
- Engine::Scene stores entities, lights, and cameras in vectors, with parallel ID vectors. ID-to-object lookups scan linearly. Entity/light/camera identity exists, but there is no general entity component store or hierarchy.
- Renderer::Entity combines transform, render flags, asset identifiers, mesh/material pointers, submesh instances, and editor-visible naming. This is straightforward for a prototype; it will be difficult to add gameplay components, transform parentage, streaming, or high-volume entity updates without further decomposition.
- Scene::Update is explicitly a placeholder. Scene mutation and behavior currently live mostly in application/editor code.
- SceneDocument provides a validated JSON boundary, schema version, asset GUIDs, and atomic save replacement. This is a strong persistence foundation, but the runtime scene model and serialized DTOs need an explicit mapping/version migration service as more component types arrive.
- AssetManager keeps project-local model/texture records and generation numbers. The renderer resource manager caches GPU meshes and textures by identity/generation and retains removed resources for a short number of submitted frames.

### 1.3 Rendering and editor

- The renderer builds main and shadow RenderViews, culls submeshes against view frusta, creates RenderPackets, sorts some queues, and uses PipelineCache to reuse graphics pipelines.
- The current graph is **not yet the central renderer**: RenderSystem builds views/lighting before graph execution, then adds only ShadowPass, ForwardPass, and DebugPass. Geometry and lighting are combined in one forward shader/pass. There is no G-buffer, separate lighting pass, post-processing, temporal history, or compute path.
- RenderGraph does dependency sorting, cycle detection, resource lifetime tracking, transient texture pooling, and basic texture state changes. Those are worthwhile foundations, but the pass contract and access model are currently texture-only and under-specified.
- The editor is a separate executable and its panels have their own files. EditorPanel has a request emitter and receives input events. The Editor implementation itself is about 1,250 lines and owns global selection, scene lifecycle, file/asset operations, command history, viewport management, camera controls, and panel event dispatch. Its current scale and responsibilities make it a coordination god object.
- The custom event interface supports synchronous panel-to-editor requests and editor-to-panel broadcasts. This is easy to follow for a small app, but events are defined in many per-event headers and the central editor still needs to know about most event types and state transitions.

### 1.4 Strengths to preserve

- NVRHI is a useful API abstraction and is already used for device, resource, framebuffer, shader, pipeline, and binding handles.
- RAII is used for NVRHI handles and D3D11 COM objects. The resource manager also recognizes GPU lifetime as distinct from C++ object lifetime.
- Pipeline creation is centralized and keyed by relevant graphics state instead of being duplicated in each pass.
- Scene documents use a version field, reject malformed values, report useful errors/warnings, and save through a temporary-file replacement.
- Asset identities and generations provide a good cache invalidation primitive. Embedded glTF images have subresource identities.
- Material GPU layout has explicit size/offset assertions and an ABI version. BRDF code has a reference test target.
- Editor commands establish a practical base for undo/redo, and scene/entity IDs allow selection state to survive some object replacements.
- GPU debug event labels and submission counters provide hooks for profiling and observability.

## 2. Code Unification and Quality

### 2.1 Naming and API consistency

The code mixes Engine and Renderer namespaces, PascalCase data members (TargetScene, MaterialBindings, m_ActiveScene), lower camel case fields (isImported, width, format), and both GetFoo() and public mutable fields. Include paths and naming generally use project-specific class names, but ownership and mutability conventions are inconsistent.

**Unification plan:**

1. Adopt one C++ style guide for project code. A practical choice is:
   - PascalCase for types.
   - camelCase for functions and local variables.
   - m_camelCase only for private members, or no prefix consistently.
   - snake_case for serialized JSON and shader ABI identifiers where interoperability benefits.
   - const references/spans for read-only borrowed data; explicit mutable APIs for edits.
2. Establish module namespace ownership:
   - Engine: application/runtime, scene, assets, platform-neutral handles.
   - Render: render world, device abstraction, graph, materials, passes.
   - Editor: UI, panels, editor commands and editor-only events.
   Migrate incrementally; do not combine naming cleanup with behavior changes.
3. Replace broad subsystem getters (GetSceneRenderer, GetResourceManager, GetRenderGraph, GetPrimitiveFactory) with narrow APIs or scoped service access. Public getters expose implementation and allow the editor/app to bypass lifecycle rules.
4. Use explicit result types for operations that can fail: expected<T, Error> or a project equivalent. Avoid nullptr plus mutable std::string& error combinations where practical. Include context (asset ID, path, GPU operation, pass) in structured errors.
5. Use enum class plus strongly typed IDs consistently. Avoid raw uint32_t and pointer identity as durable semantic identifiers.

### 2.2 Ownership and memory model

C++ handles are generally RAII-managed, but semantic ownership is less clear:

- ViewportBinding stores raw pointers to camera, viewport, and scene. RenderView stores raw scene/framebuffer pointers and raw view pointers are placed in the frame blackboard. The current application/view pool lifetime appears designed to keep these valid during a synchronous frame, but that invariant is not represented in types.
- Scene stores shared_ptr objects and duplicates IDs in parallel vectors. Shared ownership makes it difficult to determine whether the scene, editor command history, or another subsystem is responsible for object lifetime.
- RenderPacket holds GPU handles and per-object state, so draw-list construction creates many reference-counted handles.
- ResourceManager::AdvanceSubmittedFrame calls device-wide waitForIdle() before clearing retired resources when its frame threshold is reached. This is safe but stalls all GPU work and negates asynchronous retirement.

**Concrete actions:**

- Document and encode lifetimes: make frame-local view references span/reference wrappers over a frame context; use weak or stable handles only across frames; prohibit storing frame-local pointers in persistent systems.
- Make the scene the owner of runtime objects; editor commands store stable IDs plus compact before/after state rather than taking shared ownership of whole render entities when possible.
- Replace parallel ID/object vectors with a single slot-map or dense store plus ID-to-index map. Validate generational IDs and deletion/reuse behavior.
- Replace global device idle with fences/timeline values. Retire each object after the GPU completion value of its last referencing submission.
- Centralize GPU upload in a render-thread upload queue, with staging batches and explicit lifetime/fence tracking. The current RenderDevice::CreateMesh/CreateTexture creates immediate command lists and submits separately for each resource.

### 2.3 Component communication

The current editor event emitter is synchronous and direct. It is not a general event bus and does not provide queueing, subscription lifetime, threading, replay, or transaction semantics. The existing Engine::Events::EventDispatcher is a type-checking helper, while panel events are mostly dispatched by Editor::ProcessPanelEvent.

**Plan:**

- Separate three concepts: input events from the OS, editor commands (requests that mutate state), and notifications (facts that already occurred).
- Make state mutations flow through commands or application services, then publish immutable notifications. This preserves undo/redo and prevents panels from editing global state through ad-hoc calls.
- Replace owning unique_ptr<Event> with a small variant for editor-only requests where the event set is closed. Use queued dispatch for cross-thread notifications and synchronous dispatch only for local UI interaction.
- Name event ownership and scope explicitly (EditorCommand, EditorNotification, InputEvent), rather than adding editor event types to the engine input EventType.
- Add subscriber handles with automatic unsubscription if broadcast subscription grows beyond the current fixed panel list.

### 2.4 Module boundaries and include hygiene

GraphicEngineCore publicly links imgui, d3d11, dxgi, and GLFW. This propagates UI/backend dependencies to consumers of the engine shared library. Several public headers also include NVRHI, DirectXMath, or asset importer structures, creating broad transitive compile coupling.

**Plan:**

- Split into targets with one-directional dependencies:
  - EngineRuntime: scene/runtime/asset interfaces, no ImGui or concrete graphics API.
  - RenderCore: NVRHI-facing renderer and graph.
  - RenderBackendD3D11: swapchain/device integration and native interop.
  - EditorUI: ImGui panels and editor application.
  - EditorPlatformWin32: native dialogs and OS integration.
- Use PRIVATE dependencies whenever a type is not part of a public header. Move backend-specific includes into implementation files behind pimpl.
- Add include-what-you-use checks and compile each public header standalone. Reduce umbrella includes and move AssetImporter.h types out of RenderDevice.h into an asset data interface.
- Set explicit CMake target properties, warning levels, and feature options. fxc and D3D11 dependencies currently make the root build inherently Windows/DX11-specific; guard platform targets and shader compiler discovery so non-Windows configuration fails with an intentional message or builds the platform-neutral targets.

## 3. Cleanup and Technical Debt

### 3.1 Confirmed unfinished or redundant surface

| Area | Evidence | Recommendation |
|---|---|---|
| Empty cache types | renderer/render_graph/RenderCache.h contains empty FramebufferCache and BindingCache classes | Remove until implemented, or define the cache contract and owner before exposing it |
| Render queue breadth | RenderQueueType has opaque/masked/transparent/shadow/UI/skybox, but view-building pushes visible mesh packets only to Opaque | Implement real material/visibility classification before retaining unused queue semantics; otherwise remove placeholder enum values |
| Graph API breadth | RGBuilder::ImportTexture and ReadWrite exist but the current three passes do not exercise a general import/read-write workflow | Keep only if scheduled work will use them; add graph tests and validation before building on them |
| Old construction path | Renderer::EntityBuilder remains public and is used by PrimitiveFactory and SandboxApp; primitive creation has also moved into Engine::PrimitiveFactory | Keep one supported primitive path. Migrate the sandbox, then make the lower-level builder private or remove it |
| Empty/incomplete future fields | ViewportBinding::RenderMask is documented as unimplemented; ClearTarget is present while the current forward pass clears its target unconditionally | Implement and honor them, or remove until semantics exist |
| Incomplete editor notification surface | Editor events are spread across many small headers and the editor handles a large set directly | Consolidate related event definitions or use a typed variant; remove event types with no producer/consumer after reference checks |
| Duplicated light/camera/entity lookup patterns | Scene maintains parallel ID and object arrays and repeats linear lookup logic | Replace with one tested entity store/handle map |
| Legacy graphics language | Project assets compile to DXBC via required fxc; the Neural material enum value is accepted and persisted, while shader dispatch shown supports Legacy/GGX/GTR | Mark unsupported material modes explicitly in the UI/serializer or implement their shader path; do not silently present a mode as usable |
| Render-target implementation leak | ViewportTarget stores a D3D11 SRV solely to provide the viewport texture ID to UI | Move native view creation/ImGui texture registration to a D3D11 editor integration layer |

No large subsystem should be deleted solely because a symbol looks unused: use the reference scan and migration tests as part of the cleanup branch. The RenderDevice and ResourceManager are complementary (device object construction versus caching), not duplicates by themselves.

### 3.2 Cleanup sequence

1. **Inventory and freeze semantics:** generate a symbol/reference report, list supported material fields and render queue rules, then tag APIs as supported, deprecated, or internal.
2. **Remove genuinely empty or unreachable placeholders:** empty FramebufferCache/BindingCache classes; unused includes; stale comments; fields confirmed to have no use after the public API audit.
3. **Unify primitive path:** make PrimitiveFactory the only application-facing primitive creator, move geometry generation into reusable CPU mesh data, upload via the render upload queue, and migrate SandboxApp.
4. **Centralize resource uploads:** eliminate one immediate command list per mesh/texture. Batch assets imported in one editor action and report progress asynchronously.
5. **Tighten public headers:** private implementation details go into .cpp or pimpl; avoid exposing D3D11 types outside the backend/editor adapter.
6. **Retire compatibility wrappers only after consumers move:** make deprecation warnings actionable, set a removal milestone, then delete wrappers and redundant paths.
7. **Add focused tests with each cleanup:** graph dependency/lifetime tests, scene ID stability tests, importer/material mapping tests, and undo/redo round-trip tests. This audit did not add or run tests.

### 3.3 Build and dependency debt

- CMake requires fxc, D3D11 libraries, and the DX11 NVRHI backend at the project root. This is honest for current support but prevents building shared core portions independently and makes adding another backend a root-project concern.
- Shader compilation assumes vertex/pixel suffixes and Shader Model 5.0 profiles. There is no shader reflection or generated C++ binding ABI validation; resource slots are maintained by hand.
- CMAKE_CXX_STANDARD is set to C++23 project-wide. Compiler support and library version requirements should be declared centrally; the CMake minimum and actual toolchain matrix should be verified.
- clang-format and clang-tidy targets are defined for the core only, and become echo-only targets when the tools are absent. Editor source therefore has no equivalent checked formatting/lint path in this configuration.
- Tests cover BRDF reference behavior and AssetManager basics, but not RenderGraph, frame lifecycle, GPU-resource lifetime, shader ABI, scene/editor commands, or material import fidelity.

## 4. Render Passes and Graphics Systems

### 4.1 Current pass audit

| Pass/system | Current behavior | Gap and next change |
|---|---|---|
| Scene preparation | CPU culls submesh bounds, allocates per-object constant buffer, builds packet and pipeline key | Use persistent frame allocators/constant-buffer rings, stable material/mesh IDs, and instance batching. Cache or precompute bounds; instrument per-view CPU time |
| Shadow | Allocates a 2048² four-slice depth array through the graph, creates per-slice framebuffers, draws shadow queues | Shadow views are fixed in count; graph is texture-only. Make shadow allocation demand-driven, map actual shadow-casting lights to views, add cascade support for directional lights only when needed, and add bias/format/resolution settings |
| Forward | Writes the light buffer, creates scene binding sets, clears each view, draws opaque/masked/sky/transparent/UI queues | This is a forward pass, not a deferred geometry-plus-lighting pipeline. Implement alpha blend/depth-write rules and transparent back-to-front sort. Reuse scene bindings across compatible views and cache descriptor/binding sets |
| Debug | Builds CPU debug geometry and submits it after forward | Preserve as an optional graph pass; batch upload and make overlay/depth state part of graph/resource declarations |
| Post-processing | None in active graph | Add explicit output/tonemap pass before editor presentation; create color-space/output transform policy and render-scale controls |
| Compute/async work | None in project-owned active passes | Add only after graph has typed resources, usage states, capability checks, and backend queue semantics |

The geometry path also computes a world-space bounding sphere by transforming all eight AABB corners for each submesh on each view build. This is correct in spirit but duplicates work across main and shadow views. Store local bounds once, transform a conservative sphere using a cached world transform and maximum scale, and use hierarchical/batched culling as scene sizes justify.

SortViewQueues sorts Transparent by the same pipeline/material/buffer pointer ordering as opaque work. Transparent geometry requires depth ordering (usually back-to-front) within compatible blending constraints. More fundamentally, visible packets are currently appended to Opaque in CullAndFillView, and imported alpha modes are only reported as unsupported warnings. Treat transparency/masking as a correctness feature, not just an optimization.

### 4.2 Render-graph maturity and fixes

The current graph has real value: pass setup records resource dependencies, compilation topologically sorts active passes, detects cycles and missing transient producers, tracks first/last use, and recycles compatible textures. The implementation should now be hardened before it is used for more passes.

**Required graph work, in order:**

1. Define a typed resource/access API: Texture, Buffer, attachment, sampled read, storage read/write, copy source/destination, indirect args, and explicit subresource range.
2. Derive resource states from declared access and validate descriptor capabilities. Current transitions infer a whole texture state from whether a pass declared input or output; they cannot express simultaneous/read-write or subresource access.
3. Make the graph own attachments and pass execution declarations; remove hidden graph resource access where possible. Shadow and forward passes currently obtain physical resources and create framebuffer/binding state inside their Execute methods.
4. Separate graph compilation from per-frame dynamic data. Cache compiled pass order and resource plan for stable topology; invalidate on topology/descriptor changes.
5. Add transient aliasing tests and a graph debug dump (pass list, dependencies, resource lifetime, transitions, alias assignments).
6. Add explicit imported-resource initial/final states and presentation transitions. Validate null allocation failures and pass failures.
7. Introduce compute and async queues only with backend capability and synchronization models. A graph that schedules a compute pass without queue ownership and fence semantics will be misleading.

### 4.3 Resource binding and state churn

Binding sets for each material are cached on the Material, but the forward pass creates a scene binding set inside each view's execution, and per-object binding sets are created as part of packet construction. Introduce layout-compatible binding caches or bindless/descriptor-table indexing when backend limits permit. Keep stable slot assignments in a generated shader ABI rather than hand-maintaining C++ and HLSL locations.

Opaque sorting by pipeline, material binding, and buffers is a good starting point for reducing state changes. Improve it by:

- Building sort keys from stable numeric pipeline/material/mesh IDs rather than raw pointer ordering.
- Tracking pipeline and binding changes and avoiding redundant state-set work where NVRHI permits.
- Adding instancing and multi-draw/indirect submission behind a backend capability layer.
- Keeping transparent sorting separate from opaque batching.
- Recording state-change counts, pipeline switches, descriptor updates, draw count, and upload bytes in editor statistics.

### 4.4 Lighting and materials

The pixel shader evaluates all active lights for each shaded pixel and uses 3×3 PCF shadow sampling. The shader supports legacy, GGX, and GTR paths, which is a strong experimentation base. However:

- The CPU caps light data at ENGINE_MAX_LIGHTS (default 8), and the loop is per-pixel. This is acceptable for a small scene but should be measured before increasing the cap.
- Light data supports directional/position/cone variants, but the shadow mapping setup assigns shadow views only to positional shadow casters. Directional cascades are absent.
- Imported glTF includes metallic-roughness, occlusion, emissive, alpha mode, double-sided, texture coordinates, sampler settings, and normal scale fields. The current render path binds only base-color and normal maps; unsupported channels and alpha/sidedness are warned about.
- Tangents, texture coordinate sets, sampler wrap/filter state, color management, and alpha cutoff need consistent importer-to-runtime-to-shader handling.
- Shader output/color target format currently uses RGBA8_UNORM viewport color. Introduce a linear HDR render target plus explicit tone mapping and sRGB presentation when visual fidelity becomes a priority.

### 4.5 Multi-view and target semantics

RenderSystem::SubmitViews chooses the first valid binding's scene for BuildShadowData and BuildLighting and shares that blackboard light data across submitted views. Views of the same scene fit this assumption; views of different scenes do not. Change the unit of render preparation to a scene/view family (or give each view family its own lighting/shadow state). ViewportBinding::ClearTarget is not reflected in the shown pass behavior, which clears view targets unconditionally. Define clear/load/store behavior in the graph and honor binding intent.

## 5. Future-Proofing and Scalability

### 5.1 Backend modernization path

Do not port renderer code separately to Vulkan or DX12. Preserve NVRHI as the API-level seam, then isolate the remaining D3D11 assumptions and test whether NVRHI abstraction coverage meets requirements.

1. Move D3D11 swapchain/native device creation into RenderBackendD3D11.
2. Define backend-neutral surface, present, resize, and frame-in-flight contracts. Keep native handles private and expose texture/framebuffer handles or a UI texture registration service.
3. Move ImGui initialization/rendering to the editor renderer integration. The engine core should not require ImGui to render a game frame.
4. Replace DXBC-only fxc compilation with a shader build toolchain that emits/reflection-validates artifacts for each intended backend. Keep shared HLSL where practical; use generated binding metadata and explicit shader permutations.
5. Add a second backend only after the device, swapchain, shader, surface, and resource lifetime seams are backend-neutral. Validate with a small clear-and-triangle conformance sample before moving the whole engine.
6. Add backend capability queries instead of assuming optional APIs, formats, shader stages, or queue types.

NVRHI vendor sources include multiple backends, but the project currently configures NVRHI_WITH_DX11 ON and links nvrhi_d3d11; the existence of vendored implementations does not mean the application can currently run them.

### 5.2 Job system and data-oriented rendering

Parallel rendering should follow a staged path:

- **Stage A:** Separate immutable frame snapshots from mutable scene/editor objects. Main thread applies edits and builds a stable render snapshot.
- **Stage B:** Partition view culling and packet generation into jobs using read-only spans and per-job packet arenas. Merge/sort at a synchronization point.
- **Stage C:** Use worker command lists only where the chosen backend supports safe parallel recording. Keep immediate context execution and resource uploads on a controlled render thread.
- **Stage D:** Add GPU-driven culling/indirect drawing if profiling demonstrates CPU submission limits.

For ECS integration, avoid an all-at-once rewrite. First move transforms and renderables into component arrays, use generational entity IDs, and keep editor-facing names/metadata in a separate editor or scene metadata store. Render extraction should produce compact RenderInstance and LightInstance arrays rather than making SceneRenderer traverse editor-rich Entity objects.

### 5.3 Editor/runtime boundary

The editor should remain a client of runtime services:

- EngineRuntime exposes scene operations, asset IDs, diagnostics, and serialization.
- RenderCore consumes runtime snapshots and produces view targets.
- Editor owns selection, panels, undo/redo, editor camera, import UX, and UI texture registration.
- EditorServices owns commands such as create/delete/reassign/import/save. Panels issue commands; they do not directly own renderer internals.
- Editor-only state (dock layout, selection, viewport camera, debug toggles) should be stored separately from game/runtime scene data unless it is intentionally part of the serialized scene.

This split preserves one engine runtime for editor and future standalone applications, while allowing editor tooling to expand without coupling the core DLL to ImGui or Win32.

## 6. Strict Modernization Roadmap

### Phase 0 — Correctness and visibility (first)

- [Completed: `d05547b`] Fix multi-scene view lighting/shadow ownership with per-view lighting and per-scene shadow slices.
- [Completed: `2ddb8b6`] Implement material queue classification and opaque/masked/transparent draw state, including glTF alpha cutoff and sidedness.
- Honor ClearTarget and add graph-level load/store semantics.
- Stop calling waitForIdle() from ordinary frame retirement; add completion-based deferred release.
- Extend statistics with per-view cull/build time, draw/dispatch counts, pipeline switches, allocation counts, and GPU timestamps.
- Add targeted regression tests for each correction.

**Exit criteria:** two different scenes can be rendered in separate viewports in one frame with independent lights/shadows; transparent ordering is verified; resource release does not globally stall the device under normal operation.

### Phase 1 — Stable render frame and upload ownership

- Add a frame context with frame index, completion fence, transient upload arenas, and per-frame diagnostics.
- Replace per-object constant buffers with aligned ranges from a persistently mapped or frame-ring buffer where supported.
- Batch per-object binding updates and GPU uploads; introduce a render-thread upload queue.
- Encode view and render-target lifetimes in the frame context rather than raw long-lived pointers.
- Remove duplicate primitive creation path after SandboxApp migration.

**Exit criteria:** allocation counts scale with frame/view arenas, resource uploads are batched, and GPU lifetime tests pass under reimport/removal while frames are in flight.

### Phase 2 — Real render graph

- Add typed texture/buffer accesses, subresources, pass culling, failure reporting, descriptor validation, and imported resource transitions.
- Make render passes declare color/depth outputs and sampled inputs; move framebuffer creation and attachment policy into the graph.
- Add graph visualization/debug dump and deterministic tests for topology, missing producers, cycles, state transitions, and transient aliasing.
- Split forward rendering into named, observable passes only where a concrete need exists. A render graph is infrastructure, not proof of a deferred pipeline.

**Exit criteria:** all graphics resources touched in a frame are represented by the graph or explicitly marked external; transitions and lifetimes are inspectable and covered by tests.

### Phase 3 — Module and UI separation

- Create EngineRuntime, RenderCore, RenderBackendD3D11, and EditorUI CMake targets.
- Move ImGui lifecycle and D3D11 texture registration out of core.
- Remove D3D11 types from renderer-facing public headers and add backend-neutral surface/viewport API.
- Split the editor coordinator into scene/document service, command service, viewport controller, and panel/event routing.
- Move editor events out of engine input types and consolidate event definitions or use command/notification variants.

**Exit criteria:** a non-editor executable links runtime and renderer without ImGui; public render interfaces contain no D3D11 headers/types; editor actions remain undoable and independently testable.

### Phase 4 — Material/import quality and modern presentation

- Implement full glTF material mapping: metallic-roughness, normal scale, occlusion, emissive, alpha mode/cutoff, double-sided, UV sets, and sampler configuration.
- Add shader reflection/ABI generation and validate C++ GPU structures against shader layouts.
- Introduce HDR target formats, tone mapping, and consistent linear/sRGB policies.
- Add shadow quality controls and directional cascades only if target scenes need them.
- Add GPU profiling and compare forward-plus/clustered approaches against current per-pixel fixed light loops before selecting a lighting architecture.

**Exit criteria:** representative imported assets preserve expected material appearance; shader binding ABI mismatches fail during build; color output is consistent across editor viewports and final presentation.

### Phase 5 — Scale, jobs, and backend expansion

- Add stable scene handles, transform hierarchy, dirty propagation, and a render extraction snapshot.
- Parallelize CPU culling/packet construction using read-only snapshots and per-worker arenas.
- Add instancing/indirect submission after profiling confirms CPU draw submission is a limit.
- Implement a small backend conformance sample through NVRHI, then add DX12 or Vulkan using backend capabilities and completion primitives.
- Define asset streaming, cancellation, progress reporting, and background CPU import before loading larger projects.

**Exit criteria:** performance targets are measured and documented for a defined scene; job execution is deterministic at scene snapshot boundaries; backend-specific logic is contained behind backend modules.

## 7. Recommended Guardrails

- No new pass should be added without declared reads/writes, output format, lifetime, and debug label.
- No new renderer feature should communicate through editor-only event types.
- No GPU resource should be freed based only on elapsed CPU frames; use actual GPU completion values.
- No public core header should add ImGui, D3D11, Win32 dialog, or editor panel dependencies.
- No supported asset field should be parsed and persisted while silently ignored at render time; report unsupported use and maintain a feature coverage table.
- No cleanup commit should mix rename/style churn with behavior changes.
- Every optimization proposal should be validated against captured CPU/GPU timings and representative scenes.

## 8. Key Source Map

- Build and dependency configuration: CMakeLists.txt, GraphicEngineCore/CMakeLists.txt, GraphicEngineEditor/CMakeLists.txt
- Application/frame lifecycle: GraphicEngineCore/src/Application.cpp, GraphicEngineCore/include/Application.h
- Backend and native interop: GraphicEngineCore/include/RenderBackend.h, GraphicEngineCore/src/backends/D3D11Backend.cpp, GraphicEngineCore/src/ViewportTarget.cpp
- Render orchestration and per-view scene selection: GraphicEngineCore/src/RenderSystem.cpp
- Culling, packet creation, sorting, lighting: GraphicEngineCore/src/SceneRenderer.cpp
- Graph contracts and lifetime: GraphicEngineCore/src/renderer/render_graph/RenderGraph.cpp, RenderPass.cpp, RGBuilder.cpp, RGResourcePool.cpp
- GPU resource construction/caching/retirement: GraphicEngineCore/src/renderer/RenderDevice.cpp, ResourceManager.cpp
- Pipeline state cache: GraphicEngineCore/src/PipelineCache.cpp
- Scene collections: GraphicEngineCore/include/Scene.h, GraphicEngineCore/src/Scene.cpp
- Asset import and GPU material binding: GraphicEngineCore/include/AssetImporter.h, GraphicEngineCore/src/RenderSystem.cpp, GraphicEngineCore/src/renderer/MaterialSystem.cpp
- Editor coordination and panels: GraphicEngineEditor/Editor.h, GraphicEngineEditor/Editor.cpp, GraphicEngineEditor/panels/
- Editor command history: GraphicEngineEditor/commands/CommandHistory.cpp
- Shader/render material path: GraphicEngineCore/shaders/color_ps.hlsl, color_textured_ps.hlsl, brdf_common.hlsli

## Final Assessment

The repository has enough useful foundations that modernization should be incremental. Preserve the asset GUID/generation workflow, validated scene documents, pipeline cache, NVRHI resource handles, and editor command history. First correct view-local lighting and the incomplete material queue behavior; next remove GPU idle stalls and per-object allocation churn; then make the render graph and module boundaries real. Once those contracts are stable, jobs, richer lighting, post-processing, and another backend can be added without multiplying the current D3D11/editor coupling.
