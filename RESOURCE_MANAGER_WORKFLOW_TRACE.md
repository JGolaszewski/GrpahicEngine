# Resource Manager Workflow Trace

This trace records the current runtime behavior inspected before changing asset or GPU-resource ownership. It is the P1.1 baseline for the manager API and lifetime decisions. Paths below refer to the repository source tree.

## Startup and initialization

`Application::Run` calls `InitializeCore` before the derived `OnInit`. `InitializeCore` creates the window, selects and initializes the D3D11 backend (which creates the NVRHI device and swap-chain resources), then constructs `RenderSystem` with the initialized backend. `RenderSystem` creates its immediate command list, shader factory and default shaders, material system, scene renderer, primitive factory, and render graph. It reports validity based on the command list, so individual initialization failures beyond that handle are not currently aggregated. The application creates its default viewport and initializes ImGui before returning. `Editor::OnInit` then creates the active scene, camera, demo material and primitive, lights, editor viewport, panels, and initial panel state.

Sequencing constraints:

- GPU-backed manager construction belongs after backend/device initialization and before editor `OnInit`.
- Editor startup currently assumes `GetRenderSystem`, `GetMaterialSystem`, and `CreatePrimitive` succeed; import/load integration must return an error before publishing partially initialized scene state.
- The renderer has a usable GPU command list before editor scene initialization. Primitive upload currently creates a separate immediate command list per primitive.

## Primitive creation

The Hierarchy add modal emits `AddObjectRequestedEvent` with a primitive shape, transform, and initial material values. `Editor::ProcessPanelEvent` creates a renderer `Material`, asks `RenderSystem::CreatePrimitive` to build/upload the selected shape through `PrimitiveFactory` and `EntityBuilder`, then adds the returned entity to the active `Scene`. It updates selection and broadcasts `SceneChangedEvent` and `EntitySelectedEvent` after insertion.

The scene assigns a monotonically increasing runtime `EntityId`; the ID is parallel to the entity vector and is not persistent across scene reconstruction. `Scene::AddEntity` rejects null pointers. The creation event currently is not routed through `CommandHistory`.

Sequencing constraints:

- Asset/model creation should reuse the same final scene insertion, selection, and broadcast behavior so panels and render submission continue to see loaded entities.
- GPU creation must remain outside an active frame: `RenderSystem::CreatePrimitive` returns null when a frame is active. `PrimitiveFactory` opens, closes, and submits an immediate NVRHI command list itself.
- Entity and material values are directly shared between scene/editor panels and renderer packets today; adding registries must avoid a second authoritative editable copy.

## Frame submission

Each loop iteration polls window events and handles resize before update. It runs `OnUpdate` and `UpdateScenes`, clears active view bindings, calls `OnSetupViews`, and registers the default view if no derived view was supplied. `RenderSystem::BeginFrame` opens its command list, marks the frame active, clears the framebuffer, uploads dirty material constants, and resets the render graph. `SubmitViews` creates main and shadow views, builds shadow data, render packets, lighting data, and debug geometry for the first active scene. `EndFrame` compiles and executes the render graph and submits the command list, then clears the active-frame flag. ImGui is rendered and `IRenderBackend::Present` is called afterward.

Sequencing constraints:

- The active-frame guard currently covers `CreatePrimitive` and `ExecuteImmediate`, but does not provide a general queued GPU-resource creation API.
- `SubmitViews` resolves GPU packets from the live scene and material objects. Resource replacement must not invalidate handles retained by submitted commands.
- `Present` has no completion/fence result. D3D11 uses a two-buffer swap chain, but swap-chain image count alone is not proof that GPU work has completed; safe retirement needs a backend/device completion policy.

## Editing, identity, and history

The Inspector edits position live while dragging, then emits a `TransformEntityCommand` on edit completion to make the change undoable. Scale, rotation, render flags, and material values are currently edited directly and are not undoable. Material changes update the selected shared `Material` and its dirty state. `CommandHistory` owns executed command objects in undo/redo stacks; `TransformEntityCommand` stores a `weak_ptr<Entity>`, so undo/redo silently becomes a no-op if the entity is destroyed. There is no scene-switch workflow yet, and the editor does not clear history on scene replacement because replacement is not implemented.

Selection is stored as runtime IDs in `Editor` and resolved through `Scene::FindEntity/FindLight/FindCamera`. The Inspector also holds selected objects weakly. Removal/re-addition assigns a new ID; no persistent entity identity or restoration of previous selection exists.

Sequencing constraints:

- Scene replacement must clear or segment command history before commands can target the replacement scene.
- Durable IDs need a schema policy separate from the current runtime counters. Commands that retain entities/materials also retain their GPU-backed resources.
- Save-state tracking cannot be inferred from command history because most current edits bypass it.

## Shutdown and GPU lifetime

`Editor`'s derived members, including `m_ActiveScene`, are destroyed before `Application::~Application` runs. However, `Application` also retains the scene in `m_ActiveScenes`. Its destructor shuts down ImGui, explicitly resets `m_RenderSystem`, then resets the backend and window. The active-scene vector is only destroyed later during base-class member destruction, after the destructor body. Thus scene entities and their `Mesh`/`Material` NVRHI handles can outlive both `RenderSystem` and the backend/device. The material system clears its own material list during its destructor, but scene entities may still share those materials. There is no renderer idle/drain call or deferred resource retirement queue today.

Sequencing constraints and finding:

- The desired lifetime invariant is currently not satisfied: application-owned scene GPU references are not cleared before backend/device teardown. P1.2 must fix teardown ordering before introducing more GPU resources.
- A safe teardown should first stop submissions, release active scenes and editor-held GPU references, clear renderer caches, wait/drain GPU-retired resources, then destroy `RenderSystem`, backend/device, and window in a deliberate order.
- Because `Application` base destruction follows `Editor` member destruction, editor-specific panels and selection references disappear before the base can clear its scene list.
- The NVRHI shared handles may keep wrapper objects alive, but they do not establish that GPU work has completed; explicit device idle/completion behavior still needs verification against the configured NVRHI backend.
- `nvrhi::IDevice::waitForIdle()` is available in the pinned NVRHI API, and the D3D11 implementation waits for device work to finish. Use this explicit shutdown barrier before releasing renderer-owned resources, rather than relying on the two-buffer swap chain or a guessed frame delay during teardown.

## Build and runtime asset paths

The CMake presets set `ENGINE_ASSET_ROOT` to `out/build/<preset>/bin`. `GraphicEngineCore` compiles shaders under `${ENGINE_ASSET_ROOT}/shaders`, and `ShaderFactory` resolves relative shader names against `GRAPHIC_ENGINE_ASSET_ROOT` when set, otherwise the compile-time `ENGINE_DEFAULT_ASSET_ROOT`. Presets do not set an editor project root or a scene-relative asset root. The root CMake install rule installs compiled shaders under `bin/shaders`; it does not define a project asset layout or install imported content. No scene-open path selection exists.

Sequencing constraints:

- Keep compiled engine assets separate from a user-selected project asset root.
- Project root and scene-relative path policy must be established before scene serialization; GUID lookup should resolve through the asset registry rather than treating a build path or working directory as project identity.
- Runtime shader lookup is build-output-root based today and should not be repurposed as the project asset root.

## Integration boundary implied by this trace

`Application` is the natural owner of CPU asset metadata and scene persistence because it owns active scenes and outlives the derived editor callbacks. `RenderSystem` is the natural owner of the NVRHI-facing resource cache and upload boundary because it owns the device-backed factories and frame state. The current primitive path demonstrates that GPU creation is synchronous and only partially guarded; a general upload API must identify its render-safe invocation point and failure result. Scene loading should build/validate temporary CPU data and resources before replacing active scene state, then publish the normal scene/selection notifications only after commit.

This trace is source inspection, not a claim that the runtime/build paths have been exercised. Focused validation remains in P1.3.
