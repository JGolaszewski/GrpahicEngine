# x64 Debug build fix plan

## Build result

The configured `out/build/x64-debug` tree uses Ninja, MSVC `cl.exe`, and Debug settings from the `x64-debug` preset. CMake defines the executable target as `Editor`, with output name `Editor.exe`; there is no `Engine` target in the current CMake files. The build was run from the Visual Studio 2026 x64 Developer environment with:

```powershell
cmake --build out/build/x64-debug --target Editor --parallel $env:NUMBER_OF_PROCESSORS
```

The build regenerated successfully and completed the Clang-Tidy target for all 57 first-party C++ sources. Compilation stopped before linking: 24 object compilations failed with MSVC C2220, which reports that a warning was promoted to an error by `/WX`. The captured log is `out/build/Engine-x64-debug-build.log`.

The log contains 673 MSVC warning records and 3,700 Clang-Tidy warning records. Counts include repeated diagnostics when a header is compiled through multiple translation units. The top-level warning count therefore does not represent distinct source defects.

## P0 — Unblock MSVC compilation

1. **Resolve C4251 DLL-interface warnings at the public API boundary.** C4251 accounts for 651 MSVC warning records and is responsible for most C2220 failures. It repeats across public classes that expose DirectXMath values, NVRHI handles, and standard-library members. Start with `GraphicEngineCore/modules/rendering/include/renderer/Mesh.h`, `Material.h`, `Light.h`, `Entity.h`, `GraphicEngineCore/modules/scene/include/components/Transform.h`, `Scene.h`, `GraphicEngineCore/modules/rendering/include/SceneRenderer.h`, and `GraphicEngineCore/modules/runtime/include/Application.h`. Decide which types are part of the DLL ABI. Keep GPU-layout value structs layout-stable; use PIMPL or private implementation ownership for exported owning classes where it avoids exporting implementation types. Avoid globally disabling C4251.

2. **Replace unsafe environment reads.** Eight C4996 warnings come from `std::getenv` calls in `GraphicEngineCore/modules/runtime/src/LogEngine.cpp` and `GraphicEngineCore/modules/rendering/src/ShaderFactory.cpp`. Add one small environment-variable helper with an MSVC `_dupenv_s` implementation and a portable implementation for other toolchains; release the allocated buffer with RAII.

3. **Clear the remaining `/WX` warnings.** Remove or omit unused parameter names for intentional no-op callbacks in `GraphicEngineCore/modules/runtime/include/Application.h`, `GraphicEngineCore/modules/rendering/src/RenderSystem.cpp`, and `SceneRenderer.cpp`. Rename shadowing locals in `GraphicEngineCore/modules/scene/src/SceneDocument.cpp` and `GraphicEngineCore/modules/assets/src/AssetImporter.cpp`. Remove or use the unused `imported` local in `AssetImporter.cpp`. Review the C4324 padding warning for `RenderView` against its alignment and GPU data-layout requirements before changing its representation.

4. **Rebuild immediately after these groups.** The first build stops on warnings, so it may not yet expose later compile or link failures. Rerun the same `Editor` target and record newly revealed diagnostics before proceeding.

## P1 — Reduce Clang-Tidy warning volume

The Clang-Tidy target completed, but emitted about 3,700 advisory warning records. The largest categories are:

- `readability-uppercase-literal-suffix`: 1,002
- `readability-braces-around-statements`: 569
- `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access`: 518
- `modernize-use-trailing-return-type`: 413
- `readability-implicit-bool-conversion`: 271
- `readability-identifier-length`: 195
- `readability-math-missing-parentheses`: 106
- `cppcoreguidelines-pro-type-vararg`: 75
- `cppcoreguidelines-pro-type-union-access`: 64

Handle bounds, varargs, union access, and conversion findings as correctness reviews. Apply mechanical style fixes such as literal suffixes and braces in small module-scoped batches, review the diffs, then run the corresponding source set through the repository `lint` target. Keep `WarningsAsErrors` policy explicit; do not silence whole check families just to reduce the count.

## P2 — Verify delivery

1. Build `Editor` again until compilation and linking complete, confirming `out/build/x64-debug/bin/Editor.exe` exists.
2. Build the configured test targets and run CTest with `-C Debug`.
3. Run the repository `lint` target and confirm no diagnostics remain in check families configured as errors.
4. Save the final build and test summaries with the fix commits.
