---
name: engine-change-impact
description: Map the evidence-backed impact and regression surface of cross-subsystem graphics engine changes. Use before changing render graph, resource lifetime/synchronization, shader interfaces, backend APIs, asset formats, or public engine contracts.
---

# Engine Change Impact

Adapt change-impact analysis to engine boundaries. Before implementation, query
Graphify using the changed symbols or files to discover connected callers,
consumers, and systems that may need accommodation. Use focused neighbor/path
queries rather than broad prompts. For PR review, use Graphify PR impact when
available to orient the review.

## Mem0 lifecycle

- **Entry — Query:** At skill start, query Mem0 for relevant architecture decisions, subsystem boundaries, data-flow and resource-lifetime conventions, interface layouts, constants, and prior failure patterns. Verify relied-on facts against current source, docs, and tests.
- **Exit — Store:** After successful work, save newly established, verified architecture patterns, boundary decisions, or complex bug fixes as concise `[Category] [Domain]` facts. Skip documented facts and transient analysis; never store source code, secrets, or raw logs. If the MCP is unavailable or its data boundary is not approved, continue from repository evidence and report the limitation when material.

Treat graph output as a discovery aid: check that it reflects the current
checkout and verify relevant links against source, build metadata, generated
outputs, and tests. If the graph is missing or stale, note the limitation and
trace the impact with repository search and call sites. Keep this investigation
read-only before implementation; mark unknowns as unverified.

Use repository source and build metadata for architecture and subsystem
relationships. Use mem0 only as a lead to prior durable decisions and confirm
they still apply. Use sequential thinking
when tracing branching cross-subsystem dependencies or failure modes. Follow the shared
[agent tooling guidance](../../../.codex/agent-tooling.md).

## Trace

1. **Code path:** entry point, callers, render graph/pass dependencies, resource creation/use/destruction, queues/frames in flight, backend implementations, and generated sources.
2. **Contract:** CPU/GPU layouts, descriptors/registers, shader reflection, serialization, asset/importer formats, public APIs, and compatibility assumptions.
3. **Backend matrix:** supported graphics APIs, platforms, feature levels, and fallback paths touched by the change.
4. **Failure modes:** synchronization/lifetime hazards, device loss/resize/shutdown, stale assets/pipelines, validation-layer findings, and visual regressions.
5. **Proof:** exact focused unit/integration tests, shader variants, API validation, image comparisons, and manual hardware checks required by repository conventions.
6. **Recovery:** for migrations or persisted formats, describe compatibility and rollback before changing data or public contracts.

Prefer dependency evidence from source calls, build graphs, shader reflection,
generated files, CI, and tests. Do not infer impact from directory names. Add
newly discovered consumers to the task scope and provide the required change or
coordinate their owner before editing. After implementation, compare actual
diff and evidence to the planned scope and report missing checks.
