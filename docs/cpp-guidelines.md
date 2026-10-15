# C++ Guidelines

These conventions apply to first-party Core and Editor C++ unless a module has a stricter documented contract.

Types and public engine/editor callable names use PascalCase. Private data members use the `m_` prefix; local variables use descriptive lower camel case.

## Modules and dependencies

- Core modules live under `GraphicEngineCore/modules/{assets,rendering,runtime,scene}/`; Editor modules live under `GraphicEngineEditor/modules/{application,commands,events,panels,platform}/`.
- Each module owns a `CMakeLists.txt`, public headers in `include/`, and implementation files plus private headers in `src/`. Do not create monolithic subproject-level `include/` or `src/` trees or redundant same-module-name folders.
- Follow module-exported include paths. Keep public headers limited to consumer APIs; prefer forward declarations and keep implementation-only headers in `src/`. Use PIMPL when it materially reduces graphics API or third-party exposure.
- Keep dependencies explicit in each module target. Prefer lower-level facilities toward higher-level features and avoid new cycles. Extract shared types or interfaces before introducing a dependency cycle. Existing Core cross-module references are legacy, not a target architecture.

## C++23 and API design

- Fallible APIs return `Engine::Result<T>` or `Engine::Status`, with `Engine::Error` carrying diagnostics. Use `std::optional` for absence or cancellation, not operation failures. Existing output-parameter compatibility overloads remain available while callers migrate.
- Do not throw from per-frame rendering paths.
- Use RAII. Prefer `std::unique_ptr` for exclusive heap ownership and `Microsoft::WRL::ComPtr` for D3D COM interfaces. Do not manually release owned graphics resources outside their RAII owner.
- Use `std::span` and `std::string_view` for non-owning views. Avoid raw owning pointers, needless large geometry copies, and pointer-heavy hot-path structures; prefer contiguous storage and measure before performance changes.
- Do not use `using namespace std;` or namespace imports in headers. Keep public symbols in the established `Engine` namespace.
- `ENGINE_ASSERT` is not defined. Use `assert` for suitable debug invariants and `Engine::Result` for recoverable failures.
- Preserve GPU data layout explicitly. Align constant-buffer structures and validate CPU/GPU layout assumptions when they change.

## Formatting and static analysis

Use the checked-in `.clang-format`. Format only touched first-party C++ files; never format vendor code. The CMake `format` target checks formatting without modifying files.

First-party MSVC targets use `/W4 /WX /permissive- /analyze /w14242 /w14265`. `.clang-tidy` enables `bugprone-*`, `modernize-*`, `performance-*`, `cppcoreguidelines-*`, and `readability-*`; `bugprone-*` and `performance-*` diagnostics are errors. Do not suppress or bypass applicable findings. Third-party include directories must be marked `SYSTEM`; preserve existing SYSTEM treatment for ImGui, NVRHI, tinygltf, and stb.

The repository clang-tidy header filter includes `GraphicEngineCore`, `GraphicEngineEditor`, `GraphicEngineBackends`, and `SandboxApp`; `vendor/`, `out/`, and `build/` are excluded.

For preset setup, build commands, and test workflows, see [Testing and performance](testing.md).
