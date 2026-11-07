---
name: renderer-validation
description: Plan and perform focused validation for renderer, shader, GPU resource, and visual-output changes. Use whenever a change affects rendering behavior, graphics API use, shader interfaces, or GPU lifetime.
---

# Renderer Validation

Choose validation from the target repository's actual tools and available hardware. Do not assume a particular graphics API, test framework, device, or golden-image tolerance.

## Mem0 lifecycle

- **Entry — Query:** At skill start, query Mem0 for relevant validation standards, prior GPU/resource-lifetime failures, driver quirks, resource formats, and test conventions. Verify relevant guidance against current source, scripts, and hardware evidence.
- **Exit — Store:** After successful work, save newly verified validation requirements, stable hardware constraints, or complex bug post-mortems as concise `[Category] [Domain]` facts. Skip already documented facts; never store source code, secrets, raw logs, or temporary state. If the MCP is unavailable or its data boundary is not approved, continue from available evidence and report the limitation when material.

Use repository scripts, tests, and CI for validation and visual-check procedures.
For specific graphics API documentation represented in the local corpus, query
local docs RAG first and check its version context. Use official documentation
for gaps or current/version-sensitive confirmation. Use sequential thinking when a
change spans interacting GPU lifetime, synchronization, shader, or backend
contracts. For behavior or interface changes, query Graphify for connected
rendering systems and consumers, then verify the impact against source and
tests. Follow the shared [agent tooling guidance](../../../docs/agent-tooling.md).

## Before implementation

1. State the expected behavior and likely failure modes.
2. Find existing tests, shader checks, sample scenes, validation-layer setup, headless backend, and image comparison tools.
3. Select the smallest check that can fail for the intended regression. For visual changes, identify a representative scene and expected output.
4. Mark physical-GPU, driver, API-validation, and manual checks that cannot run in the current environment.

## Validation choices

- CPU-only math, serialization, state selection, and resource bookkeeping: deterministic unit/property tests where supported.
- Shader edits: compile every affected stage/variant and validate reflected bindings or generated interfaces if the project supports it.
- API/resource changes: run project validation layers or debug layer and exercise creation, resize/recreation, in-flight use, and teardown paths.
- Render output changes: use the project's image/golden test or a stable sample and capture. Apply documented tolerances and compare like-for-like build, device, resolution, and color-space settings.
- Backend abstractions: validate each supported backend when practical; state exactly which backend/platform pairs were tested.

## Report

Record the command, backend/device where relevant, result, and limits. Keep `build passed`, `API validation passed`, and `rendered output looks correct` as separate claims. Never claim a GPU check that did not run.
