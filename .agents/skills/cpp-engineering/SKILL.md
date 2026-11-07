---
name: cpp-engineering
description: Apply modern C++ ownership, lifetime, type-safety, and interface practices when implementing, reviewing, or refactoring C++ engine code. Use repository conventions and constraints where they differ from generic guidance.
---

# C++ Engine Engineering

Use this skill for C++ implementation and review in a graphics engine. First inspect local C++ rules, nearby code, compiler standard, and ownership conventions. The target repository is authoritative.

## Mem0 lifecycle

- **Entry — Query:** At skill start, query Mem0 for relevant ownership, interface, style, error-handling, and prior bug-fix conventions. Verify anything relied on against current repository evidence.
- **Exit — Store:** After successful work, save newly established, verified, reusable C++ conventions or complex bug fixes as concise `[Category] [Domain]` facts. Skip already documented facts; never store source code, secrets, raw logs, or transient state. If the MCP is unavailable or its data boundary is not approved, continue from repository evidence and report the limitation when material.

Use repository source and docs for engine guidance. For specific third-party
library documentation in the shared tooling guide's local corpus, query local
docs RAG first and verify version context. Search mem0 only for potentially useful durable decisions and confirm
they remain current. Use sequential thinking for cross-subsystem design
or tricky lifetime analysis, not routine edits. For a new feature or behavior
change, use Graphify to locate connected callers and consumers before editing;
verify them in source and accommodate newly discovered systems. Follow the shared
[agent tooling guidance](../../../.codex/agent-tooling.md).

## Core checks

- Prefer RAII and explicit ownership. Avoid leaking or duplicating ownership of GPU and CPU resources. Follow established wrapper types and destruction queues.
- Make lifetimes and thread affinity visible in APIs. Check whether a resource is used by in-flight frames before reuse or destruction.
- Prefer strong types for units, handles, states, and IDs where they prevent mix-ups. Preserve ABI and serialization contracts.
- Keep headers self-contained and avoid introducing global mutable state. Follow local naming, formatting, error, and exception policy.
- Use `const` and value semantics where they fit, but do not force immutability or generic abstractions into hot paths contrary to project patterns.
- Do not claim a change is faster without a repeatable measurement. Follow `renderer-performance` for optimization work.

## Graphics-specific review

For renderer changes, trace CPU handle lifetime through GPU submission and completion. Check synchronization, resource transitions/barriers, descriptor/bindless indexing, resize/device-loss/shutdown paths, and all supported backends that share the changed interface. Verify API-specific behavior from the exact API version's primary documentation.

## Finish

Run the narrowest relevant project command discovered from the repository. Report exact checks and unavailable GPU/device checks; do not invent a test command or framework.
