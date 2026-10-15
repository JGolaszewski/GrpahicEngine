#pragma once

#include "runtime/include/CoreExport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>

namespace Engine::Log
{
/// Severity assigned to a diagnostic event.
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

/// Subsystem that emitted a diagnostic event.
enum class Category : uint8_t
{
    Core,
    Editor,
    Physics,
    Render,
    Backend,
    Audio,
    ECS,
    Net,
    Network = Net,
    Asset,
    Diagnostics,
    Count
};

/// Number of indexed categories, excluding the Count sentinel.
inline constexpr size_t CategoryCount = static_cast<size_t>(Category::Count);

/// Maximum number of positional values stored in one event.
inline constexpr size_t MaxLogArguments = 8;
/// Maximum text bytes stored for one argument, including the null terminator.
inline constexpr size_t MaxLogArgumentText = 192;
/// Maximum format-string length accepted by the logging backend.
inline constexpr size_t MaxLogFormatLength = 384;
/// Maximum error-code bytes stored in one event, including the null terminator.
inline constexpr size_t MaxLogErrorCodeLength = 64;
/// Maximum number of structured key/value entries stored in one event.
inline constexpr size_t MaxLogPayloadEntries = 8;

/// Storage category for a bounded log argument value.
enum class ArgumentType : uint8_t
{
    Signed,
    Unsigned,
    Floating,
    Boolean,
    Text
};

/// Fixed-size, allocation-free representation of one formatted log argument.
struct LogArgument
{
    ArgumentType type = ArgumentType::Signed; ///< Active member stored in value.
    /// Storage for the active scalar or text value.
    union Value {
        int64_t signedValue;           ///< Signed integer payload.
        uint64_t unsignedValue;        ///< Unsigned integer payload.
        double floatingValue;          ///< Floating-point payload.
        bool booleanValue;             ///< Boolean payload.
        char text[MaxLogArgumentText]; ///< Null-terminated text payload.

        /// Initializes the union to a zero unsigned value.
        constexpr Value() : unsignedValue(0)
        {
        }
    } value; ///< Typed value storage selected by type.
};

/// One bounded key/value pair attached to a structured event.
struct PayloadEntry
{
    char key[32]{};      ///< Null-terminated payload key.
    LogArgument value{}; ///< Typed payload value.
};

/// Fixed-size event record that producers can enqueue without allocation.
struct LogEvent
{
    uint64_t timestampNs = 0;                ///< Wall-clock timestamp in nanoseconds.
    uint64_t monotonicNs = 0;                ///< Monotonic timestamp in nanoseconds.
    uint64_t frameIndex = 0;                 ///< Application frame associated with the event.
    uint64_t threadId = 0;                   ///< Producer thread identifier.
    uint64_t operationId = 0;                ///< Correlation identifier for a scoped operation.
    uint32_t line = 0;                       ///< Source line that emitted the event.
    Level level = Level::Info;               ///< Severity of the event.
    Category category = Category::Core;      ///< Subsystem category of the event.
    uint8_t argumentCount = 0;               ///< Number of active positional arguments.
    uint8_t payloadCount = 0;                ///< Number of active structured payload entries.
    const char *format = nullptr;            ///< Static-lifetime format string, normally a string literal.
    const char *file = nullptr;              ///< Static-lifetime source file name.
    const char *function = nullptr;          ///< Static-lifetime source function name.
    char errorCode[MaxLogErrorCodeLength]{}; ///< Optional stable machine-readable error code.
    std::array<LogArgument, MaxLogArguments> arguments{};     ///< Positional formatting arguments.
    std::array<PayloadEntry, MaxLogPayloadEntries> payload{}; ///< Structured event fields.
};

/// Returns the textual name of a severity level.
/// @param level Severity value.
/// @return Static string for the level, or a fallback for invalid values.
ENGINE_API const char *ToString(Level level) noexcept;

/// Returns the textual name of a logging category.
/// @param category Category value.
/// @return Static string for the category, or a fallback for invalid values.
ENGINE_API const char *ToString(Category category) noexcept;

/// Parses a category name without allocating.
/// @param name Category name to parse.
/// @return Matching category, or Category::Core when no name matches.
ENGINE_API Category CategoryFromName(std::string_view name) noexcept;
} // namespace Engine::Log
