#pragma once

#include <cstdint>

namespace Renderer
{
inline constexpr uint32_t MaxTextureCoordinateSets = 8;
}

#ifdef _WIN32
#ifdef EXPORTING_ENGINE_CORE
#define ENGINE_API __declspec(dllexport)
#else
#define ENGINE_API __declspec(dllimport)
#endif
#else
#define ENGINE_API
#endif
