---
name: editor-feature-integration
description: Trace and expose GraphicEngine Core, scene, asset, or rendering capabilities through complete Editor workflows, persistence, and feedback.
---

# Engine Feature Integration in the Editor

Use this skill when a feature crosses an engine subsystem and the Editor, or
when an existing engine capability needs a usable Editor entry point. Read
`docs/architecture.md` and the relevant Editor or subsystem guides first.

## Mem0 lifecycle

- **Entry — Query:** At skill start, query Mem0 for relevant Core/Editor boundaries, prior feature-integration decisions, pipeline or scene conventions, and cross-layer bug fixes. Verify relied-on guidance against current source, build metadata, and docs.
- **Exit — Store:** After successful work, save newly established, verified integration patterns, durable state/persistence rules, or resolved complex cross-layer bugs as concise `[Category] [Domain]` facts. Skip documented facts; never store source code, secrets, raw logs, or transient notes. If the MCP is unavailable or the project data boundary is not approved, continue from repository evidence and report the limitation when material.

## Trace the feature

1. Identify the engine owner, public API/data contract, runtime state, producers,
   and all current consumers. Use focused Graphify queries and verify results in
   code, build metadata, tests, and generated interfaces.
2. Choose an Editor affordance that matches the workflow: panel, hierarchy or
   Inspector control, menu action, viewport interaction, or settings UI. State
   how the user sees availability, failure, and completion.
3. Follow the existing request path: panel emits a typed event, the Editor
   application coordinates and calls Core, and panels receive notifications or
   refreshed snapshots. Avoid direct engine mutation from widget code.
4. Assign state to the correct owner. Authored scene/project data belongs in its
   established serialized format; machine-local preferences belong in Editor
   settings. Preserve validation, compatibility, and failure reporting.
5. For undoable user actions, inspect current `ICommand` and `CommandHistory`
   integration before adding or claiming undo support. Do not assume every
   Editor mutation currently participates in history.
6. Update focused engine and Editor documentation, public API comments, tests,
   and the manual workflow evidence appropriate to the affected layers.

Keep coupled engine and Editor contract changes with one coding owner. Preserve
scene/entity and GPU resource lifetimes, frame synchronization, and shutdown
ordering. Report unverified Editor, backend, or rendered behavior explicitly.
