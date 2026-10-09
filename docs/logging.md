# Logging

The engine logger lives in `GraphicEngineCore/include/LogEngine.h`. Producers enqueue fixed-size events to a bounded asynchronous queue. The log worker writes JSONL and retains an in-memory breadcrumb ring. Subscribed callbacks run on a separate worker with a bounded queue, so slow observers cannot stall file output or log producers. Callback overflow and exceptions are counted in logger health; callbacks should still be fast and thread-safe.

## Configuration

Configure the logger with environment variables before process startup:

| Variable | Default | Purpose |
|---|---|---|
| `ENGINE_LOG_PATH` | `%LOCALAPPDATA%/GraphicEngine/logs/engine.jsonl` on Windows; `$XDG_STATE_HOME/graphic-engine/engine.jsonl` or `$HOME/.local/state/graphic-engine/engine.jsonl` elsewhere | JSONL path. An explicitly empty value disables the file sink. |
| `ENGINE_LOG_BREADCRUMB_PATH` | `engine-crash-breadcrumbs.log` beside the JSONL file | Latest fatal/shutdown breadcrumb snapshot. |
| `ENGINE_LOG_LEVEL` | `Trace` | Global runtime minimum: `Trace`, `Debug`, `Info`, `Warning`, `Error`, `Fatal`, or `None`; numeric values 0–6 are accepted. |
| `ENGINE_LOG_<CATEGORY>_LEVEL` | `Trace` | Optional category minimum. Supported suffixes: `CORE`, `EDITOR`, `PHYSICS`, `RENDER`, `BACKEND`, `AUDIO`, `ECS`, `NET`, `NETWORK`, `ASSET`, and `DIAGNOSTICS`. |
| `ENGINE_LOG_MAX_FILE_SIZE_BYTES` | 16 MiB | Rotate when the active file reaches this size. Zero disables rotation. |
| `ENGINE_LOG_RETAINED_FILE_COUNT` | 3 | Number of numbered rotated files to retain. |

Errors and fatal events bypass runtime level filtering. Compile-time `ENGINE_LOG_LEVEL_MIN` still strips lower-severity call sites from the build. The editor console can change global and per-category runtime minimums while the application is running.

## Event conventions

- Use the established category for the owning subsystem. Backend API/device diagnostics use `Backend`; logger and telemetry diagnostics use `Diagnostics`.
- Give errors stable lowercase dotted identifiers such as `backend.d3d11.present.device_removed` or `asset.model.load_failed`. Keep the code stable when human-readable wording changes.
- Include the identifiers and values needed to reproduce the failure as typed format arguments or `PayloadEntry` values. Avoid encoding machine-readable data only in prose.
- Use `Engine::Log::ScopedOperation` around a user-visible or multi-step operation. Nested scopes keep the current operation ID; the logger copies it onto every event on that thread.
- Keep per-frame measurements in metrics snapshots rather than writing a log event for every frame.

```cpp
Engine::Log::ScopedOperation operation;
ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.present.failed",
                      "Present failed | hresult: 0x{:X} | width: {} | height: {}",
                      static_cast<uint32_t>(hr), width, height);
```

For structured fields, construct bounded entries with `Engine::Log::MakePayloadEntry` and pass them to `Engine::Log::SubmitStructured` with `std::source_location::current()`. The convenience `ENGINE_LOG_*_CODE` macros attach a stable error code and source location.

## Health and retention

`Engine::Log::GetHealthSnapshot()` reports file-sink state/path, session ID, accepted and written events, queue depth/capacity/high-water mark, queue and callback-queue drops, callback failures, file failures, and breadcrumb overwrites. A file-open failure returns `false` from `Initialize` while leaving the in-memory logger active; the failure is also sent to the platform debug output and stderr. Check this result and surface the health snapshot instead of silently treating it as healthy.

`SubscribeCallback` returns a token that must be passed to `UnsubscribeCallback`; removing one subscriber does not affect other sinks. `Flush` waits up to five seconds and emits a fallback diagnostic if it times out. Fatal events and application shutdown persist the recent breadcrumb ring to the configured breadcrumb path.
