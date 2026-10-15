# Dead code candidate review on final main

Date: 2026-10-15. Source: local `main` at `616a68c`.

The reviewed set covers prior clang-tidy findings, renderer and render-graph headers moved into private implementation folders, compatibility paths, and dependencies in `vcpkg.json`.

| Candidate | Evidence | Disposition |
|---|---|---|
| Former unused `bindingError` local in `Application.cpp` | Absent; final main clang-tidy processes 77 translation units with no diagnostics. | Resolved. |
| Former unused `error` local in editor save handling | Save path consumes the error status; no final clang-tidy diagnostic. | Resolved. |
| Renderer and render-graph private headers | Compiled by the full x64 Debug build after moving backend-facing declarations from public include folders to `src`. | Retained as private implementation interfaces. |
| Legacy `parentId` and material migration | Persistence and application round-trip/migration tests cover the compatibility paths. | Retained for serialized document compatibility. |
| Legacy logging callback | Public API adapts the single callback slot through the subscription mechanism. | Retained for API compatibility. |
| `directxmath`, `glfw3`, `nlohmann-json`, `tinygltf`, `stb`, `gtest` | Used by transforms/rendering, windowing, scene/assets, glTF import, image decoding, and correctness tests. | Required dependencies. |
| Optional Google Benchmark | Used by the opt-in transform, traversal, scene-save, and render-graph benchmark target. | Required only when benchmark feature is enabled. |
| Vendored NVRHI and ImGui | Used by renderer/backend integration and editor UI integration. | Required vendored components. |

No confirmed unused project-owned symbol or dependency remains among the reviewed candidates.
