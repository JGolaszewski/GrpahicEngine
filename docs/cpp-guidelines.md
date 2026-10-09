# C++ Guidelines

Core and editor C++ use the checked-in `.clang-format` configuration. Types use PascalCase, callable names use PascalCase for public engine/editor APIs, and private data members use an `m_` prefix. Keep local variables descriptive and use lower camel case.

Fallible APIs return `Engine::Result<T>` or `Engine::Status`, with `Engine::Error` carrying the diagnostic. Optional values represent absence or cancellation; they are not substitutes for operation failures. Existing compatibility overloads that use output parameters remain available while callers migrate.

Run the `format` and `lint` CMake targets to check both `GraphicEngineCore` and `GraphicEngineEditor`. The format target rejects files that differ from the repository style. The lint target treats enabled clang-tidy diagnostics as errors.
