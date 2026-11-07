---
name: renderer-performance
description: Investigate or optimize frame time, GPU/CPU bottlenecks, memory use, bandwidth, or frame pacing with measured baselines and correctness gates. Use when a task asks to make rendering or engine workloads faster.
---

# Renderer Performance Loop

Do not optimize before defining the workload, correctness gate, metric, baseline, and bounded experiment budget.

## Mem0 lifecycle

- **Entry — Query:** At skill start, query Mem0 for comparable workload baselines, device/driver quirks, prior bottleneck findings, and project performance conventions. Verify comparability against original captures and current conditions.
- **Exit — Store:** After successful work, save a new measured baseline or verified, reusable performance finding with workload, build, backend, device/driver, resolution, and capture method, tagged `[Category] [Domain]`. Skip undocumented transient hypotheses; never store secrets, source code, or raw logs. If the MCP is unavailable or its data boundary is not approved, continue with repository evidence and report the limitation when material.

Use repository scripts and CI for benchmark and capture procedures. Query local
docs RAG only when a change depends on Google Benchmark API details. Search mem0
for prior workload or baseline context only as a lead; confirm comparability
from the original capture and current environment. Use sequential thinking to compare competing bottleneck
hypotheses or plan controlled experiments. Follow the shared [agent tooling
guidance](../../../.codex/agent-tooling.md).

## Baseline

Record as available: scene/workload, build configuration, target backend, device/driver, resolution, warm-up, sample count, capture/profiling tool, CPU frame time, GPU frame time, frame pacing, memory, and bandwidth. Separate CPU bottlenecks from GPU bottlenecks and present/wait time.

## Experiment

1. Locate the hot path with a profiler or trace; do not infer it from source shape alone.
2. State one measurable hypothesis and make one controlled variant.
3. Run the same workload and correctness/render-output gate under comparable conditions.
4. Repeat noisy measurements and report variation; reject changes that regress correctness or reproducibility.
5. Keep a small ledger of baseline, variant, command/capture procedure, result, and conclusion.

Do not trade away validation, portability, stability, image quality, or required synchronization for a performance result unless the user explicitly changes the requirement.

## Finish

Report the best measured safe result, the evidence, the exact workload/tool conditions, and any unresolved platform differences. Do not call it a global optimum.
