---
name: editor-ui-development
description: Implement or review GraphicEngine Editor panels, menus, widgets, input behavior, docking, and UI preferences using existing Dear ImGui and event patterns.
---

# Editor UI Development

Use this skill for visible Editor GUI changes. Read `docs/editor-guide.md`,
`docs/editor-settings.md`, or `docs/editor-workflows.md` as applicable, and
inspect the nearest panel and its event paths before choosing an implementation.

## Mem0 lifecycle

- **Entry — Query:** At skill start, query Mem0 for relevant panel/widget, input/event, UI preference, and prior regression conventions. Verify relied-on details against current Editor source and docs.
- **Exit — Store:** After successful work, save newly established, verified UI conventions or resolved complex UI bugs as concise `[Category] [Domain]` facts. Skip documented facts; never store source code, secrets, raw logs, or transient state. If the MCP is unavailable or the project data boundary is not approved, continue from repository evidence and report the limitation when material.

## Editor boundaries

- Panels implement `EditorPanel::Draw()` and use `OnEvent()` for application notifications. Send user requests through typed events with `EmitEvent()`; keep Core or scene mutations in the Editor/application handler.
- Preserve `EditorEventRouter` propagation semantics. Input handlers that consume an event set `Handled`; subscriptions are RAII connections owned with the panel lifetime.
- Follow the existing Dear ImGui style, stable widget IDs, selection behavior, and UI frame/init/shutdown lifecycle. Keep panel state valid across scene replacement and entity deletion.
- Decide whether a value belongs to the authored scene, a project asset, or local Editor preferences before adding persistence. Avoid putting local layout/session choices in scene data.
- Keep UI changes focused. Do not introduce a new UI framework, general widget library, or broad visual redesign unless the task requires it.

## Workflow and validation

1. Trace the visible control to its state source, request event, application handler, and any resulting notification or persistence path.
2. Implement with existing panel and event patterns. Coordinate Core/API changes with their owning layer instead of bypassing its contract from ImGui code.
3. Update relevant Editor help when visible behavior or controls change. Find focused tests and build commands from repository docs/CI; add the manual UI check needed to confirm the interaction when the environment supports it.
4. Report exact checks and separate compile/test results from observed UI behavior. State any unavailable window/GPU validation.
