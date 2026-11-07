---
name: shader-pipeline-validation
description: Validate changes to shader sources, build variants, reflection, and CPU/GPU interface contracts. Use when editing shaders or their compiler/build/runtime pipeline.
---

# Shader Pipeline Validation

Follow the target repository's actual shader toolchain and pipeline. Do not assume HLSL, GLSL, SPIR-V, DXC, glslang, or a specific renderer backend.

## Mem0 lifecycle

- **Entry — Query:** At skill start, query Mem0 for relevant pipeline states, constants, buffer/layout alignments, resource formats, shader/CPU interface conventions, and prior pipeline failures. Verify all relied-on details against current source, generated interfaces, and build scripts.
- **Exit — Store:** After successful work, save newly established, verified shader/interface conventions, constants, or complex bug fixes as concise `[Category] [Domain]` facts. Skip documented facts; never store source code, secrets, raw logs, or transient state. If the MCP is unavailable or its data boundary is not approved, continue from repository evidence and report the limitation when material.

Use build scripts, shader sources, and generated interfaces for engine
pipeline conventions. For DirectX, Vulkan, or NVRHI API documentation represented
in the local corpus, query local docs RAG first and check its version context. Use
Exa through graphics-api-research for corpus gaps or current/version-sensitive
confirmation. Use sequential thinking when following several
shader stages or CPU/GPU contracts. When a shader or layout change is planned,
use Graphify to find connected CPU producers and rendering consumers, then
verify the relationships in source and generated interfaces. Follow the shared
[agent tooling guidance](../../../.codex/agent-tooling.md).

1. Trace source files through includes/macros, variant generation, compiler invocation, validation/optimization, reflection, generated bindings, packaging, and runtime pipeline creation.
2. Enumerate affected stages, entry points, permutations, target environments, and supported backend/API versions from project manifests and scripts.
3. Check CPU/GPU interface layout: bindings, registers/sets, offsets/strides, descriptor types, push constants, vertex inputs/outputs, formats, and specialization constants.
4. Compile all changed variants using the pinned project toolchain. Run available validators/reflection checks and focused consuming-code tests.
5. If GPU execution or image comparison is possible, validate one representative scene and record backend/device/driver. Otherwise mark runtime behavior unverified.

Report exact commands and variants checked, changed generated artifacts, and unavailable runtime coverage. Never edit generated output in place unless project documentation says it is authoritative.
