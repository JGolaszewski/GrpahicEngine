# GraphicEngine

GraphicEngine is a Windows-first graphics engine built with C++23. The repository contains reusable engine Core modules, a Direct3D 11 rendering backend, an editor application, and a sandbox application.

## Project overview

- **Core:** runtime, asset, scene, and rendering modules.
- **D3D11 backend:** rendering and platform integration for Windows.
- **Editor:** a desktop application for working with engine scenes and assets.
- **Sandbox:** a small application target for experimenting with the engine.

This project is actively developed. See the documentation for the current behavior and API details.

## Build and run

The documented quick-start workflow targets Windows x64 with Visual Studio 2022 or newer, CMake 3.28 or newer, Ninja, vcpkg, `VCPKG_ROOT`, and `clang-tidy`. Shader compilation also requires the Windows SDK `fxc` compiler. Run the commands from a Visual Studio Developer PowerShell at the repository root.

Configure and build the Debug preset:

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug --parallel $env:NUMBER_OF_PROCESSORS
```

The build places the applications in `out/build/x64-debug/bin`:

```powershell
& .\out\build\x64-debug\bin\Editor.exe
& .\out\build\x64-debug\bin\Sandbox.exe
```

For tests, other build configurations, and toolchain details, see [Testing and performance](docs/testing.md) and [Windows validation](docs/validation/windows.md).

## Documentation

- [Documentation home](docs/index.md)
- [C++ guidelines](docs/cpp-guidelines.md)
- [Development workflow](docs/development-workflow.md)
- [Testing and performance](docs/testing.md)
- [Windows build and renderer validation](docs/validation/windows.md)
- [Logging](docs/logging.md)
- [Rendering: shadows](docs/rendering/shadows.md)
- [Core API reference](docs/api-reference.md)

## Contributing

Before making changes, read [AGENTS.md](AGENTS.md) and the [development workflow](docs/development-workflow.md). The project is licensed under the [Apache License 2.0](LICENSE).
