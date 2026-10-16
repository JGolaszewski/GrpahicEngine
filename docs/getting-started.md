# Getting started

This guide takes a Windows x64 checkout from prerequisites to a running Editor
or Sandbox build. The checked-in `x64-debug` preset is the supported quick-start
path; see [Testing and performance](testing.md) for test, lint, benchmark, and
other build workflows.

## System requirements

The documented workflow uses:

- Windows 11 x64.
- Visual Studio 2022 or newer with the **Desktop development with C++**
  workload and a Windows SDK.
- CMake 3.28 or newer and Ninja, available on `PATH` in the developer shell.
- vcpkg, with `VCPKG_ROOT` pointing to its checkout.
- `clang-tidy`, available on `PATH` or in Visual Studio's LLVM tools.
- The Windows SDK `fxc` shader compiler, available on `PATH`.

Run the commands below from **Developer PowerShell for Visual Studio** so the
MSVC compiler and Windows SDK tools are initialized. The CMake presets are
Windows-only; a generic CMake installation on another platform does not make
this preset workflow portable.

Check the key tools before configuring:

```powershell
cmake --version
ninja --version
clang-tidy --version
Get-Command cl.exe, fxc.exe
Test-Path "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

If `VCPKG_ROOT` is unset, set it in the current shell to your vcpkg checkout,
for example:

```powershell
[Environment]::SetEnvironmentVariable('VCPKG_ROOT', 'C:\src\vcpkg', 'Process')
```

Use the path where you actually cloned vcpkg. Persist it in your user
environment settings if you want it available in future shells.

## Dependencies

The root `vcpkg.json` manifest and its pinned builtin baseline describe the
third-party packages used by the build. The CMake preset selects the
`x64-windows` triplet and vcpkg toolchain, then manifest mode installs packages
during the first configure. The project uses DirectXMath, GLFW, nlohmann-json,
OpenImageIO, stb, and tinygltf; GoogleTest is used by the test targets. The
optional `benchmarks` feature adds Google Benchmark and is not needed for the
normal build.

Install or update vcpkg using its official Windows setup instructions before
configuring. Keep `VCPKG_ROOT` set in the same Developer PowerShell session.
You do not need to manually install the manifest dependencies. The first
configure can take time while vcpkg downloads and builds them; subsequent runs
reuse the installed packages under the build tree.

## Configure, build, and run

From the repository root, configure and build Debug:

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug --parallel $env:NUMBER_OF_PROCESSORS
```

The preset writes generated build files and vcpkg's manifest installation under
`out/build/x64-debug/`. Executables and runtime DLLs are placed in
`out/build/x64-debug/bin/`; static libraries go in `out/build/x64-debug/lib/`.
The preset also sets the default engine asset root to the `bin` directory, and
the D3D11 shader build writes compiled shaders under `bin/shaders/`.

Launch either application from the repository root:

```powershell
& .\out\build\x64-debug\bin\Editor.exe
& .\out\build\x64-debug\bin\Sandbox.exe
```

The Editor opens the interactive scene and asset authoring application. Sandbox
is a small standalone application for engine experiments. A successful build
confirms compilation and linking; rendering still requires a Windows machine
with a working Direct3D 11 graphics device and driver.

For another configuration, use the corresponding configure preset such as
`x64-release`. To build and run tests, see [Testing and
performance](testing.md). To build and preview this documentation site, see
[Documentation home](index.md#build-the-documentation).

## Repository map

| Path | Purpose |
| --- | --- |
| `GraphicEngineCore/` | Reusable engine code organized into `assets`, `rendering`, `runtime`, and `scene` modules. Builds the `GraphicEngineCore` shared library. |
| `GraphicEngineBackends/D3D11/` | Direct3D 11 renderer backend and HLSL shaders. Builds `RenderBackendD3D11` and compiles shaders into the configured asset root. |
| `GraphicEngineEditor/` | Editor application and its `application`, `commands`, `events`, `panels`, and `platform` modules. Builds `Editor.exe`. |
| `SandboxApp/` | Small executable entry point used to exercise the engine independently of the editor. Builds `Sandbox.exe`. |
| `docs/` | MkDocs guides and reference material. |
| `scripts/` | Repository build, validation, and documentation helper scripts. |
| `vendor/` | Third-party dependencies checked into the repository. Treat as read-only when contributing. |
| `out/` | Local build and generated documentation output; ignored by Git. |

The root `CMakeLists.txt` wires these projects together. Tests live with Core
under `GraphicEngineCore/tests/`, including editor-related test sources where
they exercise shared behavior.

## Common setup problems

- **CMake cannot find the vcpkg toolchain or packages:** confirm `VCPKG_ROOT`
  points to the vcpkg checkout (the directory containing `scripts/`) in the
  same shell, then rerun configure.
- **`clang-tidy` is required or missing:** install the LLVM tools or enable the
  Visual Studio LLVM toolchain, then verify `clang-tidy --version` works. CMake
  stops during configuration when it cannot find the executable.
- **CMake cannot find `fxc`:** install or enable the Windows SDK component that
  provides the legacy Direct3D shader compiler and ensure `fxc.exe` is on
  `PATH`. The D3D11 backend requires it at configure time.
- **`cl.exe` is not found:** reopen Developer PowerShell for Visual Studio with
  the x64 C++ tools installed.
- **A dependency build fails during the first configure:** check network access
  and vcpkg diagnostics, resolve the reported package or toolchain issue, and
  rerun `cmake --preset x64-debug`.
