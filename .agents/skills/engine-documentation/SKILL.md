---
name: engine-documentation
description: Write or update GraphicEngine user, developer, architecture, workflow, and public API documentation alongside implementation changes. Use when documented behavior or an API contract changes.
---

# Engine Documentation

Keep documentation aligned with the checked-in implementation and established docs structure. The code, public headers, and observed validation results are authoritative.

## Workflow

1. Identify the audience and documentation surface: user guide, workflow/setup guide, architecture page, feature guide, or public-header API comments.
2. Find and update the closest existing page before proposing a new one. Check `docs/index.md`, `mkdocs.yml`, nearby docs, and public headers for existing conventions.
3. Make claims only when supported by current source, tests, CI, or observed behavior. Preserve distinctions between implemented behavior, limitations, and unverified GPU/platform coverage.
4. Update public-header comments when public API behavior or contracts change. Keep examples and snippets consistent with the actual API and commands.
5. Add a new documentation page to `mkdocs.yml` navigation. Avoid duplicating procedural details owned by `docs/development-workflow.md`, `docs/testing.md`, or another authoritative guide.
6. Use this repository's documented documentation build, `scripts/build-docs.ps1`, when verification is requested or required by the task. Report the exact result; a successful local build does not claim site deployment.

## References

- [Documentation index](../../../docs/index.md) and [MkDocs navigation](../../../mkdocs.yml)
- [Development workflow](../../../docs/development-workflow.md)
- [Testing and performance](../../../docs/testing.md)
- [C++ guidelines](../../../docs/cpp-guidelines.md)

If build dependencies or generated API inputs are unavailable, state that limitation and do not guess that documentation validation passed.
