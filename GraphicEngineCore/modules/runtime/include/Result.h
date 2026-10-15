#pragma once

#include <expected>
#include <string>
#include <utility>

namespace Engine
{
/// Error payload carried by failed engine operations.
struct Error
{
    std::string Message; ///< Human-readable description of the failure.
};

/// Expected-style result value whose error payload is Engine::Error.
template <typename T> using Result = std::expected<T, Error>;

/// Status result for operations that return no value on success.
using Status = Result<void>;

/// Creates an error payload from a diagnostic message.
/// @param message Human-readable failure description.
/// @return Error containing the moved message.
inline Error MakeError(std::string message)
{
    return {std::move(message)};
}
} // namespace Engine
