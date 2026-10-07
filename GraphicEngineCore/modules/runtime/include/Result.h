#pragma once

#include <expected>
#include <string>
#include <utility>

namespace Engine
{
struct Error
{
    std::string Message;
};

template <typename T> using Result = std::expected<T, Error>;

using Status = Result<void>;

inline Error MakeError(std::string message)
{
    return {std::move(message)};
}
} // namespace Engine
