---
name: engine-environment-audit
description: Inventory a graphics engine's actual source, toolchain, SDKs, shader pipeline, tests, validation layers, devices, and profilers before filling setup instructions or diagnosing environment gaps. Use for onboarding or setup audits; do not install tools.
---

# Engine Environment Audit

Establish the real environment from repository evidence before writing setup claims. This adapts ECC's codebase-onboarding workflow to native engine development.

## Mem0 lifecycle

- **Entry — Query:** At skill start, query Mem0 for relevant compiler flags, target configurations, dependency paths, build-cache settings, and known environment quirks. Treat these only as leads; confirm installed tools and setup facts from live and repository evidence.
- **Exit — Store:** After successful work, save newly verified, durable toolchain constraints, environment quirks, or build-configuration changes as concise `[Category] [Domain]` facts. Skip documented facts; never store secrets, source code, raw logs, or temporary state. If the MCP is unavailable or its data boundary is not approved, continue from available evidence and report the limitation when material.

Use manifests, presets, CI, and the live environment to establish installed
tooling. Use Exa only when a current external SDK or tool fact needs verification,
preferring its official documentation. Do not treat mem0 as evidence of installed
tools. Follow the shared [agent tooling guidance](../../../.codex/agent-tooling.md).

## Inspect

1. Identify languages and standards from build manifests, compiler flags, and source; map C++, Rust, shader, scripting, and generated-code boundaries.
2. Find OS/compiler matrices, CMakePresets or equivalent build presets, package locks, SDK versions, submodules, CI images, and documented environment variables.
3. Trace shader source to compiler, validation/reflection, generated artifacts, and runtime loading. Record tool versions and variant sources if present.
4. Discover build, test, format, static analysis, sanitizer, headless/sample, API validation, and image comparison commands from scripts and CI.
5. Record actual accessible device/API/driver evidence only when available. Identify optional capture/profiling tools from project docs; do not infer them from source dependencies.

## Report

Create a concise matrix: component, version/source, required target, observed evidence, setup path, and gap/unknown. Separate project-required tools from optional diagnostics and vendor-specific tools. Mark unsupported or unavailable environments explicitly.

Do not install SDKs, fetch packages, run unknown setup scripts, alter PATH, or probe protected systems. Use the findings to populate the project's AGENTS.md and external-tools guide. Do not invent commands to fill missing facts.
