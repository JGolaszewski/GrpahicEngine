#pragma once

#include <cstdint>

namespace Engine
{
using EntityId = uint64_t;
using LightId = uint64_t;
using CameraId = uint64_t;

inline constexpr EntityId InvalidEntityId = 0;
inline constexpr LightId InvalidLightId = 0;
inline constexpr CameraId InvalidCameraId = 0;
} // namespace Engine
