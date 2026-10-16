# GraphicEngine

GraphicEngine is a Windows-first C++23 graphics engine with a D3D11 rendering backend, reusable Core modules, and an editor application.

Use this site to find project conventions and development workflows:

- [Getting started](getting-started.md)
- [Architecture](architecture.md)
- [Rendering pipeline](rendering/pipeline.md)
- [Editor guide](editor-guide.md)
- [Asset pipeline](asset-pipeline.md)
- [Profiling, debugging, and QA](profiling-qa.md)
- [C++ guidelines](cpp-guidelines.md)
- [Rendering: shadow behavior](rendering/shadows.md)
- [Logging](logging.md)
- [Testing and performance](testing.md)
- [Windows build and renderer validation](validation/windows.md)
- [GraphicEngine API reference](api-reference.md)

## Build the documentation

Install Doxygen and Python on the machine, then create the local docs environment and build the site from the repository root:

```powershell
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
./scripts/build-docs.ps1
```

The first run creates `.venv-docs` and installs the pinned MkDocs, MkDoxy, and Markdown lint packages into it. The generated site, Doxygen XML, and API Markdown are written under `out/docs/` and are not source files.

The documentation workflow checks relevant pull requests and deploys successful
builds from `main` to GitHub Pages.

Pass `-Serve` to preview the generated site locally:

```powershell
./scripts/build-docs.ps1 -Serve
```
