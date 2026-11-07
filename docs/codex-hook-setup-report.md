# Codex Hook Setup

This proposal has been implemented. Project-local hooks check protected paths,
agentic setup, changed C++ and shader/JSON inputs, and provide graphics-specific
validation reminders. An explicit interactive GPU diagnostic session reports
NVRHI messages from a fresh log. Broad builds, tests, ASan, visual checks, and
GPU captures are not attached to general edit events.

See the [Codex Hook Audit](codex-hook-audit.md) for current findings and the
[Codex hooks guide](codex-hooks.md) for registered behavior, limits, and commands.
