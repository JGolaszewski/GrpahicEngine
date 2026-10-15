#pragma once

#include <cstdint>

namespace Engine::Renderer
{
enum class FaceCullMode : uint8_t
{
    None,
    Front,
    Back
};

enum class PrimitiveTopology : uint8_t
{
    Triangles,
    Lines,
    LineStrip,
    Points
};
} // namespace Engine::Renderer
