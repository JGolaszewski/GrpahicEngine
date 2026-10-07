#pragma once

#include "CoreExport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>

namespace Engine::Log
{
enum class Level : uint8_t
{
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Fatal = 5,
    None = 6
};

enum class Category : uint8_t
{
    Core,
    Physics,
    Render,
    Audio,
    ECS,
    Net,
    Asset
};

inline constexpr size_t MaxLogArguments = 8;
inline constexpr size_t MaxLogArgumentText = 192;
inline constexpr size_t MaxLogFormatLength = 384;
inline constexpr size_t MaxLogErrorCodeLength = 32;
inline constexpr size_t MaxLogPayloadEntries = 8;

enum class ArgumentType : uint8_t
{
    Signed,
    Unsigned,
    Floating,
    Boolean,
    Text
};

struct LogArgument
{
    ArgumentType type = ArgumentType::Signed;
    union Value
    {
        int64_t signedValue;
        uint64_t unsignedValue;
        double floatingValue;
        bool booleanValue;
        char text[MaxLogArgumentText];
        constexpr Value() : unsignedValue(0) {}
    } value;
};

struct PayloadEntry
{
    char key[32]{};
    LogArgument value{};
};

// Fixed-size and trivially copyable so producers can enqueue without allocation.
struct LogEvent
{
    uint64_t timestampNs = 0;
    uint64_t frameIndex = 0;
    uint64_t threadId = 0;
    uint32_t line = 0;
    Level level = Level::Info;
    Category category = Category::Core;
    uint8_t argumentCount = 0;
    uint8_t payloadCount = 0;
    const char *format = nullptr; // Must have static lifetime (normally a string literal).
    const char *file = nullptr;
    const char *function = nullptr;
    char errorCode[MaxLogErrorCodeLength]{};
    std::array<LogArgument, MaxLogArguments> arguments{};
    std::array<PayloadEntry, MaxLogPayloadEntries> payload{};
};

ENGINE_API const char *ToString(Level level) noexcept;
ENGINE_API const char *ToString(Category category) noexcept;
ENGINE_API Category CategoryFromName(std::string_view name) noexcept;
} // namespace Engine::Log
