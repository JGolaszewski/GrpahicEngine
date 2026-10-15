#pragma once

#include <expected>
#include <string>
#include <utility>

namespace Engine
{
/// Error information returned by a failed engine operation.
struct Error
{
    std::string Message;
};

/// Result of an operation that may fail, carrying either a value or an Error.
template <typename T> using Result = std::expected<T, Error>;

/// Status result for an operation that can fail without returning a value.
using Status = Result<void>;

/// Create an Error containing the supplied diagnostic message.
inline Error MakeError(std::string message)
{
    return {std::move(message)};
}
} // namespace Engine
