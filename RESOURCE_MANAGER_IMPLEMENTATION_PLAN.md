# Resource Manager: Remaining Work Plan

## Goal

Finish the editor-facing asset workflow on top of the existing asset, scene, and renderer foundations. The remaining work is texture asset identity and GPU support, material assignment and persistence, project-aware editor integration, and final integration cleanup.

## Implemented baseline

The following work is already present and is not part of the remaining checklist:

- vcpkg dependencies and CMake integration for TinyGLTF, stb, and nlohmann-json.
- CPU glTF/GLB import, model metadata sidecars, GUID lookup, mesh/node/material import data, and tangent support.
- Renderer mesh caching by GUID and generation, model entity creation, and deferred mesh retirement.
- Versioned JSON scene serialization, transactional runtime scene replacement, primitive descriptors, and model GUID persistence.
- Editor New/Open/Save/Save As actions, unsaved-change prompts, model import/browser, and hierarchy model instantiation.
- Undo/redo for entity add/remove and current material property edits; scene replacement clears command history.
- Scene dirty tracking for the current inspector and entity workflows.

Implementation history: afed078, 6c57091, f820b38, f6e696a, and 0ca8fb5.

## Remaining implementation

Complete these steps in order. Keep each independently reviewable change in its own commit.

### 1. Make project and scene file workflows project-aware

- Replace the editor's compile-time ENGINE_PROJECT_ROOT source-tree path with an explicit project-root lifecycle. Keep compiled engine assets separate from user project assets.
- Define how the editor opens or creates a project, how the root is remembered, and how scene paths are canonicalized and kept within the chosen root.
- Ensure New/Open/Save/Save As retain the active scene and file path on cancellation or failure. Keep the existing transactional Application::LoadScene behavior.
- Review unsaved-state coverage for every persisted scene edit, including model/material assignment, imported material overrides, camera/light edits, and direct UI edits. Keep dirty state distinct from command history.
- Show actionable errors and all relevant recoverable warnings without silently dropping all but the first warning.

**Acceptance:** scene files and referenced assets resolve relative to the selected project, save/open failures leave the active scene and path intact, and every persisted edit prompts before destructive scene replacement.

### 2. Complete texture asset identity and import

- Extend the asset registry beyond its current model-only records and GetKnownModels list.
- Support standalone PNG/JPEG imports with adjacent .meta GUID sidecars, validation, useful display names, and decoded CPU pixels. Reuse the importer's existing image decoding policy where practical.
- Give embedded model images stable subresource identities tied to the model GUID and image index. Keep external glTF image URI resolution constrained to the project root.
- Define and enforce duplicate-GUID, copied-sidecar, missing/corrupt metadata, rename/move, reimport, and importer-generation behavior for both models and textures.
- Make the browser enumerate known project assets after startup, not only assets imported or loaded during the current editor session.

**Acceptance:** standalone and embedded images have stable identities, invalid or duplicate metadata produces actionable diagnostics, and the browser lists known assets after reopening the project.

### 3. Add renderer-owned texture resources and material bindings

- Extend RenderDevice and renderer-side ResourceManager to create and cache textures and samplers by asset/subresource identity and generation. Keep NVRHI handles out of AssetManager APIs.
- Route GPU creation through the renderer's safe upload boundary. Define rollback on partial upload failure and invalidation/replacement behavior during reimport.
- Extend Material and MaterialSystem with renderer-owned texture bindings while retaining the existing constant-buffer material path for untextured primitives.
- Define texture format, color-space, channel, orientation, mip, and sampler policy. Use sRGB handling for color/emissive textures and linear data for normal and metallic/roughness maps.
- Use predictable fallback maps when optional maps are absent.
- Retain replaced texture, sampler, and binding resources until GPU completion is established. The current mesh retirement path uses a frame count and calls waitForIdle during retirement; verify its actual safety/performance behavior and use a completion mechanism where supported. Do not treat swapchain image count as proof of GPU completion.

**Acceptance:** GPU texture resources are cached and replaced safely, including shutdown, while untextured materials and existing shadow bindings remain valid.

### 4. Complete PBR texture rendering and scene persistence

- Bind and render base-color and normal maps first. Use the imported tangent and handedness data to build a transform-correct tangent basis, including non-uniform entity transforms.
- Preserve non-conflicting shader registers and document the material binding ABI alongside the existing shadow bindings.
- Preserve imported metallic/roughness factors and texture channels. Keep unsupported imported fields available for round-trip or report them clearly.
- Extend SceneMaterialDocument and JSON validation/serialization with durable texture GUID references for the supported map slots. Scene files must not contain source paths, renderer handles, or transient cache keys.
- Resolve required references transactionally during scene load; distinguish a missing required model from a missing optional texture and keep the prior active scene on required-resource failure.
- Decide how scene-level material overrides refer to model material slots and ensure each submesh's material state is saved and restored.

**Acceptance:** assigned color and normal maps render, survive save/reopen, and missing optional maps use documented defaults with visible diagnostics.

### 5. Finish editor asset and material workflows

- Expand the Assets panel to import and browse models and standalone textures, showing friendly names, asset types, GUIDs, source paths relative to the project, and import/load errors.
- Add explicit reimport for known assets. Preserve GUIDs, update generations, and refresh live scene instances transactionally so the browser, registry, GPU cache, and rendered scene do not disagree.
- Extend the Inspector to show the selected model GUID and per-submesh material slots. Let users manually assign or replace an albedo texture and a normal texture on an already-existing scene object, including generated primitives and individual model submesh material slots; assignments must work independently of the textures authored in the imported glTF.
- Let users choose an already imported texture from the Assets panel or import a standalone image as part of the assignment flow. Show the texture semantic and missing-resource state, display friendly asset names, and persist GUID references rather than file paths.
- Keep panel code event-driven. Panels should request import, assignment, or reimport; AssetManager owns file/metadata operations and RenderSystem owns GPU resource operations.

**Acceptance:** a user can import/select a model, then manually add or replace albedo and normal textures on an existing primitive or model object, select the intended model material slot, see missing references clearly, and save/reopen those overrides.

### 6. Complete undo/redo for assignments

- Add commands for model reassignment, per-slot material reassignment, and texture-reference changes. Commands must retain old and new asset/material references for redo and must not delete imported source assets.
- Treat manual texture choices as editable scene overrides on the existing object/material slot; preserve the imported material defaults so clearing an override restores the source material behavior.
- Extend material-property undo to cover the selected model submesh/material slot rather than only Entity::material (which currently aliases the first submesh material).
- Make entity membership undo restore stable runtime identity and selection state. Current add/remove commands retain the entity and ID, but selection restoration should be defined for both the affected entity and the selection that preceded the operation.
- Clear or segment history on New/Open/project switch. Keep saved-state tracking independent of whether the undo stack is empty.

**Acceptance:** supported entity, model, material, and texture edits undo and redo against the same scene and material slot; imported files remain intact.

## Integration analysis and required framework fit

These findings describe where the remaining work must integrate with the existing engine:

- **Ownership is already split in the right direction.** Application owns AssetManager; RenderSystem owns ResourceManager/RenderDevice; the editor communicates through panel events. Preserve this split as texture support is added. Do not pass NVRHI objects or JSON types through editor events or asset-manager APIs.
- **The asset model is narrower than the UI goal.** AssetManager stores only models today. Its model list is an in-memory view of loaded/imported records, not a complete project registry. Add typed records and project enumeration without making ImGui panels scan files or parse sidecars.
- **The material renderer is the main missing engine integration.** RenderDevice creates meshes only, ResourceManager caches meshes only, and MaterialSystem binds only a constant buffer. RenderSystem::CreateModelEntity currently warns that imported material textures are not bound. Texture assignment UI must wait for these bindings and fallback behavior; a GUID-only UI would appear to work while rendering nothing.
- **Model materials need slot-aware editing.** Model entities store separate submesh materials but expose the first material through Entity::material. The Inspector currently edits that first material. Add an explicit selected material slot and carry its identity through material events, commands, and scene serialization.
- **Project-root policy is still a development shortcut.** The editor compiles the source path into ENGINE_PROJECT_ROOT, while engine shaders use ENGINE_ASSET_ROOT. Replace the compile-time project-root assumption with a project lifecycle and keep the two roots independent.
- **Reimport must update all layers together.** Asset import generations and mesh cache generations exist, but live entities can keep old submesh/material references after reimport. Reimport should stage CPU and GPU data, update affected scene instances only after success, and retire replaced GPU resources safely.
- **The UI host is platform-coupled.** Win32 common dialogs currently live in Editor.cpp and Editor links comdlg32. Move file selection behind a small platform/file-dialog service if the editor is expected to keep the existing window abstraction or support another host. Keep dialog policy out of AssetManager.
- **Retirement is safe-first but potentially stalls.** Mesh retirement waits for device idle once an entry ages past the configured frame count. Preserve the proven shutdown idle wait; evaluate a backend completion/fence path for ordinary retirement before expanding the queue to textures and bindings.
- **History and dirty tracking have separate roles.** Scene replacement clears history and dirty state is tracked separately. Preserve that design, but route every persisted edit through a single dirty notification and make command callbacks restore selection consistently.
- **The workflow trace is now historical.** RESOURCE_MANAGER_WORKFLOW_TRACE.md predates the resource manager, scene persistence, and editor commits listed above. Update it after implementation, or label it as a pre-implementation trace so it is not mistaken for current behavior.

## Final cleanup and refactoring

Do this after the functional changes are complete, so refactoring follows the final ownership and data flow:

1. Consolidate repeated editor operations for scene mutation, entity insertion/removal, selection broadcasts, dirty notifications, and error/warning reporting. Keep event types focused and avoid adding panel-to-manager shortcuts.
2. Move Win32 file-dialog code out of Editor.cpp behind the chosen platform service and remove platform link dependencies from editor targets that do not need them.
3. Review public headers for dependency leakage and API duplication. Keep AssetManager CPU-only; keep RenderDevice, NVRHI handles, and renderer caches renderer-owned.
4. Review model/material/texture identity and serialization for one canonical source of truth. Avoid duplicate material state between glTF defaults, scene overrides, runtime materials, and inspector caches.
5. Review cache invalidation, rollback, GPU retirement, and destruction ordering for models, textures, samplers, binding sets, and materials.
6. Update RESOURCE_MANAGER_WORKFLOW_TRACE.md, CMake source lists, and user-facing asset documentation to match the finished behavior.
7. Run the focused asset/scene/renderer validation and a clean Visual Studio CMake configure/build. Record actual results; do not mark an acceptance item complete based only on compilation of the editor executable.

## Final completion criteria

The remaining work is complete when the editor can import and browse model and texture assets, instantiate models, manually add or replace albedo/normal textures on existing primitives and model material slots, save and reopen those GUID-based overrides, undo/redo supported scene/material assignments without changing another scene or deleting imported files, and shut down without unsafe GPU resource release.
