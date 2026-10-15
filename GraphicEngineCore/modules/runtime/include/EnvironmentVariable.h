#pragma once

#include <cstdlib>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Engine
{

inline std::optional<std::string> ReadEnvironmentVariable(std::string_view name)
{
    const std::string terminatedName(name);
#ifdef _WIN32
    char *buffer = nullptr;
    std::size_t bufferSize = 0;
    const int status = _dupenv_s(&buffer, &bufferSize, terminatedName.c_str());
    const std::unique_ptr<char, decltype(&std::free)> value(buffer, &std::free);
    if (status != 0 || value == nullptr)
        return std::nullopt;

    return std::string(value.get());
#else
    const char *value = std::getenv(terminatedName.c_str());
    return value == nullptr ? std::nullopt : std::optional<std::string>(value);
#endif
}

} // namespace Engine
