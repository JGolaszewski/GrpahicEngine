# Dead code and dependency candidate review

Date: 2026-10-15
Scope: candidates surfaced during the refactor, current clang-tidy scan, rendering header-boundary changes, compatibility code, and the vcpkg manifest.

| Candidate | Evidence | Disposition |
|---|---|---|
| `bindingError` local in `Application.cpp` | No current declaration; final branch clang-tidy run reports no diagnostics. | Resolved. |
| `error` local in editor save handling | Save path consumes the error status; final clang-tidy run reports no diagnostics. | Resolved. |
| Renderer and render-graph headers formerly exposed below `modules/rendering/include` | Implementations and call sites compile after moving backend-facing declarations to `modules/rendering/src`; full Debug build passes. | Retained as private implementation interfaces; visibility cleanup, not dead code. |
| Legacy `parentId` scene field and legacy material workflow | Scene persistence and application load/save tests cover round-trip and migration paths. | Retained for document compatibility. |
| Legacy logging callback slot | Public API documents it as the compatibility callback; implementation adapts it through callback subscriptions. | Retained for API compatibility. |
| `directxmath` | Used by transforms, scene objects, cameras, and rendering. | Required dependency. |
| `glfw3` | Used by window creation and editor platform integration. | Required dependency. |
| `nlohmann-json` | Used by scene documents, asset metadata, and editor serialization. | Required dependency. |
| `tinygltf` | Used by glTF/GLB asset import. | Required dependency. |
| `stb` | Used by image decoding during asset import. | Required dependency. |
| `gtest` | Used by the registered correctness suite. | Required test dependency. |
| Optional Google Benchmark feature | Used by transform, scene traversal, scene processing, and render-graph benchmark cases. | Required only when `ENGINE_BUILD_BENCHMARKS` is enabled; intentionally optional in normal builds. |
| Vendored NVRHI and ImGui | NVRHI backs renderer integration; ImGui backs editor UI integration. | Required vendored components. |

Final branch evidence: `integration-clang-tidy-final.log` processed 77 translation units without actionable diagnostics; `integration-full-final-build.log` and `integration-full-final-ctest.log` pass. No confirmed unused project-owned symbol or dependency remains among the reviewed candidates. Repeat this review on final `main`.
