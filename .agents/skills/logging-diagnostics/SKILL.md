---
name: logging-diagnostics
description: Add or review GraphicEngine log events, user-visible diagnostics, and operational error reporting using the existing asynchronous logger and structured logging conventions.
---

# Logging and Diagnostics

Use the existing logger and error-reporting paths. Read [Logging](../../../docs/logging.md) and inspect nearby subsystem call sites before changing diagnostic behavior. Logging supplements error propagation and user feedback; it does not replace them.

## Choose the event

- Select severity by operational meaning: trace/debug for detailed investigation, info for meaningful state transitions, warning for recoverable degradation or fallback, error for an operation that failed, and fatal only for an unrecoverable condition. Do not report expected retries or successful fallbacks as errors.
- Use the established category for the owning subsystem. Use `Backend` for backend API/device diagnostics and `Diagnostics` for logger health.
- For actionable failures, use the structured `ENGINE_LOG_*_CODE` form with a stable lowercase dotted identifier. Keep the code stable when message wording changes; do not invent a new code for routine high-frequency informational events without a consumer need.
- Include bounded typed context needed to reproduce or distinguish the event. Use structured payload entries when fields need machine-readable access. Do not log credentials, secrets, or unbounded data.
- Use `Engine::Log::ScopedOperation` around user-visible or multi-step operations so related events share an operation ID.
- Keep per-frame or per-sample measurements in metrics snapshots, not one log event per frame.

## Preserve behavior and verify

- Preserve the function's return/error contract and surface an actionable message through the appropriate user-facing path. Do not swallow a failure merely because it was logged.
- Inspect compile-time log stripping and runtime severity/category filters when deciding whether an event is available in the relevant build.
- For logging changes, check relevant logger health, callback, event, or subsystem tests. Assert observable behavior rather than brittle full message text when practical.
- Report exact checks and distinguish an emitted log event from a successfully persisted or displayed diagnostic.

## References

- [Logging conventions and configuration](../../../docs/logging.md)
- [Logger public API](../../../GraphicEngineCore/modules/runtime/include/LogEngine.h)
- [Logging macros](../../../GraphicEngineCore/modules/runtime/include/LogMacros.h)
- [Testing and performance](../../../docs/testing.md)
