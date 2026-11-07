# Codex hooks

`.codex/config.toml` registers narrow project-local hooks for protected-path
edits, agentic setup consistency, changed-file checks, and explicit GPU
diagnostic smoke sessions. They provide feedback for relevant tool events; they
do not replace CI, task-scoped verification, or human review. Codex requires
project hooks to be trusted before execution. Review the hook definitions and
scripts in `/hooks` before trusting them.

## Agentic setup consistency checker

[`scripts/check_agentic_setup.py`](../scripts/check_agentic_setup.py) validates
the custom agent registry and role files, skill frontmatter and unique names,
allowed reasoning efforts, and local links in agentic workflow instructions. It
uses only Python's standard library and runs in CI through
`.github/workflows/agentic-setup.yml`.

The project registers this checker after file-edit tools. It
does not build the engine, run tests, format files, or contact external services.
When the checker reports invalid agent or skill configuration, the hook returns
Codex's supported `PostToolUse` blocking response with its diagnostics. The edit
has already happened and cannot be undone by this hook. If the checker cannot be
launched or times out, the hook reports a non-blocking skip. CI remains the
authoritative setup check.

Run the standard-library hook tests locally with:

```powershell
python -m unittest discover -s scripts/tests -v
```

These tests exercise hook event parsing, protected-path decisions, changed-file
feedback, and setup-check failure reporting. They simulate Codex hook payloads;
they do not verify registration or execution inside the Codex client.
The agentic setup workflow runs the suite on pull requests that change the hooks,
their checker, the PR handoff validator, or the test files.

## Graphics-engine hook concepts

The implemented concepts below are registered in `.codex/config.toml`.
Verification receipts and role-specific subagent context remain unregistered.
Add future hooks only when their event matching and file scope can be tested
reliably and runtime is appropriate for the event frequency.

| Concept | Event and scope | Hook behavior | Status and constraints |
|---|---|---|---|
| Agentic setup consistency | `PostToolUse` after file edits; checker filters for agent, skill, and workflow files | Run `scripts/check_agentic_setup.py` | Invalid configuration returns a blocking response. Checker launch and timeout problems are non-blocking skips. |
| Changed-file C++ format check | `PostToolUse` after edits to first-party C++ files | Run pinned clang-format in check-only mode; never rewrite files | A detected formatting violation blocks the original tool result. Missing, mismatched, or unavailable clang-format skips with context. |
| Shader and asset input check | `PostToolUse` after edits to shader sources/includes or JSON/GLTF inputs | Incrementally build `CompileShaders` when configured Debug outputs exist; parse changed JSON/GLTF syntax | Invalid JSON or a compiler-reported shader error blocks the original tool result. Missing build outputs and unavailable commands skip with context. JSON parsing does not validate asset schema. |
| Change-aware validation reminder | `PostToolUse` after edits to renderer, backend, shader, scene, or asset source | Add short context naming the relevant manual or test workflow | Implemented; informational only and does not claim validation ran. |
| Vendor and generated-path guard | `PreToolUse` for path-bearing edit tools; target `vendor/` and known generated/build outputs | Deny edits to protected paths | Implemented for structured edit paths. A pull-request workflow checks protected paths authoritatively; shell commands may evade the local hook. |
| Verification receipt | `PostToolUse` after build/test commands | Persist command categories and exit results | Not implemented at the user's request. |
| GPU diagnostic smoke result | `PostToolUse` after `scripts/run_gpu_diagnostic_smoke.ps1` | Launch selected Editor with an isolated JSONL log; after exit, summarize NVRHI warning/error counts from that run | Validation errors, NVRHI errors, and known failed session statuses return a blocking response. Warnings remain informational. It does not verify visual scenarios. |
| Role-specific subagent context | `SubagentStart` matched to selected agent types | Inject role-specific validation and handoff reminders | Not implemented at the user's request. |

### Scope boundaries

- Keep the changed-file format check read-only. The expected clang-format
  version is pinned in `.codex/hooks/clang-format-version.txt`. A missing or
  mismatched executable, launch error, or timeout skips the check and reports
  the reason without blocking the tool result.
- Keep shader compilation, shader variant coverage, API validation, image
  comparison, capture, and GPU tests in explicit task workflows. The available
  compiler and GPU device can vary by machine, and a generic edit event does
  not establish that runtime rendering was validated.
- Keep asset checks limited to validators that are quick and deterministic.
  Integration tests that import or process project assets remain explicit
  checks described by the testing workflow.
- A validation finding returns the documented `PostToolUse` JSON response with
  `decision: "block"`, a concise `reason`, and diagnostics in
  `hookSpecificOutput.additionalContext`. Codex replaces the tool result with
  this feedback and resumes the model; the completed edit or command cannot be
  rolled back. Informational reminders and unavailable-check skips return
  `additionalContext` only and do not block.
- For protected-path checks, use `PreToolUse` only where the hook can inspect
  structured file paths. Shell command parsing and indirect writes are
  incomplete; the pull-request diff workflow is the authoritative check. A failed,
  timed-out, or malformed hook response must not be assumed to have blocked an
  operation.
- A verification receipt should record observed command results only. It must
  not infer that GPU output was correct from a successful compile or CPU test,
  and should avoid persisting raw logs or unrelated command arguments.
- GPU validation requires a running application, a suitable device, and a
  reproducible scenario. Debug-layer output can expose API misuse, but a hook
  cannot infer visual correctness from the fact that the application launched.
  `scripts/run_gpu_diagnostic_smoke.ps1` uses a fresh temporary log and reports
  only NVRHI diagnostics observed during that Editor session. It does not verify
  that the validation layer was active. Keep PIX captures and manual rendering
  scenarios explicit.
- `SubagentStart` context supplements the assigned role and skill; it does not
  replace them. Avoid repeating the full root instructions or injecting broad
  repository context into every subagent, which adds token cost and can drift.
- Do not run a full build, lint, CTest suite, ASan, docs build, or GPU workload
  after every edit. Select those checks by task and use the documented commands.
- Do not treat a hook as a safety boundary. `PostToolUse` feedback arrives
  after the action; command parsing in `PreToolUse` should not replace repository
  policy or reliable workflow checks.

## Build and test verification

Do not attach builds, lint, CTest, ASan, shader compilation, documentation builds,
or GPU validation to general edit events. Use the exact commands and scope from
[Testing and performance](testing.md) and the relevant validation skill.

Run ASan explicitly when appropriate:

```powershell
cmake --preset x64-asan
cmake --build out/build/x64-asan --parallel $env:NUMBER_OF_PROCESSORS
ctest --test-dir out/build/x64-asan --output-on-failure -C Debug
```

Hook commands resolve scripts from the Git root, so they also work when Codex
starts in a subdirectory. Set explicit short timeouts for fast hooks. Keep hook
stdout limited to the Codex JSON contract and send diagnostics to stderr. Tests
validate payload handling and response shapes, but hook registration and
execution have not been exercised inside the installed Codex client.

Run the opt-in interactive GPU diagnostic session from PowerShell after building
the Editor:

```powershell
./scripts/run_gpu_diagnostic_smoke.ps1 -EditorPath ./out/build/x64-debug/bin/Editor.exe
```

Exercise the relevant rendering scenarios in the Editor, then close it. The
script keeps its per-run JSONL log and compact report under
`%TEMP%\GraphicEngine\GpuDiagnosticRuns`. Review logs before sharing them; they
may contain local paths or diagnostic text.

## References

- [Codex hooks](https://learn.chatgpt.com/docs/hooks): lifecycle events,
  matching, trust review, tool coverage, and hook failure behavior.
- [D3D11 debug layer](https://learn.microsoft.com/en-us/windows/win32/direct3d11/using-the-debug-layer-to-test-apps):
  API diagnostics available in a running application.
- [D3D11 software layers](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-layers):
  debug-layer validation and performance considerations.
- [PIX GPU captures](https://learn.microsoft.com/en-us/windows/pix/gpu-captures/gpu-captures-overview):
  interactive GPU capture workflow.
