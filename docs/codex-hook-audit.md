# Codex Hook Audit

**Audit date:** 2026-11-07
**Scope:** Current GraphicEngine hook scripts/config/docs, ECC `graphics-engine-codex` hook guidance, and official Codex hook documentation.
**Audit type:** Static review with implementation follow-up. Project hooks are registered; Codex-client runtime and hook trust behavior have not been verified.

## Executive summary

GraphicEngine now registers narrow project hooks for protected paths, agentic setup consistency, changed-file formatting and input checks, validation reminders, and explicit GPU diagnostic sessions. ECC also ships no enabled engine hook and warns that generic format/build-on-edit automation can change user files, incur machine-specific costs, and claim checks unrelated to the change. The broad edit/build and ASan auto-trigger scripts remain removed.

The former hooks ran an in-place formatter, a full Debug build, lint, and unit/integration tests after edits, or triggered a full ASan workflow after any CTest command. Those broad automation paths have been removed; ASan and engine verification remain explicit task-scoped workflows.

Full builds, lint, CTest suites, ASan, visual checks, and PIX captures remain task-scoped. The GPU diagnostic session is explicit and reports only NVRHI messages from its own fresh log. A pull-request workflow enforces the protected-path rule because local hooks cannot cover arbitrary shell writes.

## Current behavior

| Hook | Trigger | Actual work | Audit |
|---|---|---|---|
| Agentic setup checker | CI and project `PostToolUse` after file edits | Standard-library TOML/frontmatter/link consistency checks | Invalid configuration returns a supported blocking response; launch errors and timeouts are informational skips; CI is authoritative |
| Protected-path edit guard | Project `PreToolUse` for file-edit tools; pull-request workflow over changed paths | Reject edits to `vendor/`, build/generated directories, and compiled outputs | Local guard is limited to structured edit paths; CI diff check is authoritative |
| Changed-file input checks | Project `PostToolUse` after file edits | Pinned clang-format check, JSON/GLTF syntax parse, incremental shader build when configured outputs exist | Actual format, JSON, or shader validation failures return a supported blocking response; missing tools, timeouts, and missing prerequisites are non-blocking skips |
| Validation reminder | Project `PostToolUse` after relevant renderer/shader/asset edits | Adds task-relevant docs and manual validation guidance | Context only; it does not claim checks ran |
| GPU diagnostic session | Explicit `scripts/run_gpu_diagnostic_smoke.ps1` Editor launch; project `PostToolUse` summarizes its report | Fresh JSONL capture and NVRHI warning/error counts | Known failed statuses or logged NVRHI errors block the returned tool result; warnings remain informational; no automated visual scenario assertion |
| C++ edit/build hook | Removed | Formerly formatted, built, linted, and tested after each eligible edit | Heavy and mutating; use explicit task verification |
| ASan CTest hook | Removed | Formerly configured/built ASan and ran its full suite after any CTest command | Broad and surprising; use explicit ASan commands |

`PostToolUse` blocking feedback does not undo the completed edit or command. Codex replaces the tool result with the hook's reason and context, then resumes the model. Informational reminders and unavailable-check skips return context without a blocking decision.

## ECC comparison

ECC's `hooks/README.md` intentionally has no enabled hook. Its SessionStart example is ECC plugin-runtime context bootstrap, not engine verification. ECC recommends adding hooks only when there is a stable command and a clear event, keeping scripts narrow, verifying the installed Codex schema, and preferring explicit skills/commands when automation is not needed. Its two future candidates are a deterministic scoped formatter/check and a result-only collector that does not rerun costly workloads.

GraphicEngine now uses file-scoped hooks and keeps expensive engine checks explicit. The shader hook invokes only the configured incremental `CompileShaders` target after shader edits and skips if the existing Debug outputs are incomplete.

## Recommended hook set

### Registered project profile

`.codex/config.toml` registers the protected-path guard, agentic setup checker, graphics changed-file checks, and a Bash report reader for the explicit GPU diagnostic command. Project-local hooks require review and trust in the installed Codex client. The PR workflow independently enforces protected paths.

### Changed-file C++ format check

The registered synchronous `PostToolUse` handler checks changed first-party C/C++ files with the pinned formatter version, never rewrites them, and skips when that version is unavailable. It does not build C++ targets, run lint, or run CTest.

The expected formatter version is recorded in `.codex/hooks/clang-format-version.txt`; a PATH version mismatch is reported as a skipped check.

### Agentic configuration consistency check

The standard-library checker is registered after edit tools and filters to agentic configuration/docs. CI runs the same checker as the authoritative consistency gate.

The local hook blocks only when the checker reports actual metadata or link errors. If the checker process cannot start or times out, the hook reports a non-blocking skip. It does not replace the CI check.

### Keep explicit rather than automatic

- ASan configure/build/full-suite run: explicit documented verification, not triggered by arbitrary CTest use.
- Full Debug build, `lint`, and unit/integration suites: task-level checks or CI, not every edit.
- Documentation site build: explicit, since `scripts/build-docs.ps1` creates a venv and installs pinned Python packages when needed.
- Full shader permutations, API validation, capture, image comparison, visual scenarios, and GPU tests: explicit renderer/shader workflow. The edit hook only compiles changed shader inputs incrementally when the configured output tree is already complete.
- SessionStart, UserPromptSubmit, Stop, SubagentStop, verification receipts, and role-specific subagent context remain unregistered. Avoid repeated prompt injection, workflow control loops, and command-text policy parsers.

## Implementation and schema notes

- Official Codex docs support hooks for local tool calls including `apply_patch`, `Edit`, `Write`, shell, and MCP paths. Match specific events and parse event payloads rather than guessing changed paths from arbitrary command strings.
- Repo-local hook commands should resolve scripts from the Git root. Codex may start in a subdirectory. Keep Windows-specific `command_windows` only when it changes invocation behavior.
- Set explicit timeouts; official documentation gives 600 seconds as the default for most hooks, which is too long for fast feedback.
- Keep synchronous hooks only when the model needs immediate feedback. Asynchronous hooks are suitable for noncritical reporting, but their results must not be treated as a gate that the model has already observed.
- Keep hook stdout within the documented JSON contract and send diagnostics to stderr.
- Test event payload variants, path normalization/exclusions, nested-directory launch, skip conditions, success/failure, timeout, malformed input, and Windows command selection before enabling.
- Review the exact hook definition and script as executable code before trusting/enabling it. A `PostToolUse` failure cannot undo the completed operation; use a supported pre-tool gate only for a narrow precondition that cannot be checked more reliably in normal workflow.

## Prioritized actions

1. **Done:** removed the automatic ASan trigger and broad edit/build/test hook.
2. **Done:** registered scoped project hooks for protected paths, changed-file checks, and validation context.
3. **Done:** pinned the formatter expectation and added JSON/GLTF syntax plus incremental shader input checks.
4. **Done:** added protected-path CI enforcement and an explicit fresh-log GPU diagnostic workflow.
5. **Done:** retained the agentic consistency CI check and registered a local feedback hook.
6. **Open:** verify hook registration, payload coverage, and output behavior in the installed Codex client. Hooks were not executed during this implementation.

## Evidence and limitations

- Reviewed `.codex/config.toml`, `.codex/hooks/*.py`, `docs/codex-hooks.md`, `docs/codex-hook-setup-report.md`, `docs/agentic-development.md`, and `scripts/build-docs.ps1`.
- Compared with `C:\Users\jgolasz\source\ECC\graphics-engine-codex\hooks\README.md` and its setup report.
- Mem0 lookup failed with local connection refused; it was not used as evidence.
- Unit tests invoke the hook scripts with simulated Codex event payloads and validate response shapes. The GPU launcher and hook registration/execution inside the Codex client were not exercised.

## References

- [Official Codex Hooks documentation](https://learn.chatgpt.com/docs/hooks)
- ECC local reference: `C:\Users\jgolasz\source\ECC\graphics-engine-codex\SETUP-REPORT.md` (not published with this site)
