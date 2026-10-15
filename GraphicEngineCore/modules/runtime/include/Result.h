#pragma once

#include <expected>
#include <string>
#include <utility>

namespace Engine {
/// Diagnostic returned when an engine operation fails.
struct Error {
  std::string Message;
};

/// Value-or-error result for an operation that may fail.
template <typename T> using Result = std::expected<T, Error>;

/// Result for an operation that can fail without producing a value.
using Status = Result<void>;

/// Construct an error from a diagnostic message.
inline Error MakeError(std::string message) { return {std::move(message)}; }
} // namespace Engine
