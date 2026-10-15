#pragma once

#include "runtime/include/CoreExport.h"

#include <DirectXMath.h>

namespace Engine::Renderer
{
/// Vertex attributes shared by imported models and built-in primitives.
struct Vertex
{
    /// Creates a default-initialized vertex.
    Vertex() = default;

    /// Creates a vertex with position, normal, and the first UV set.
    /// @param position Object-space position.
    /// @param normal Object-space normal.
    /// @param uv Texture coordinates for UV set zero.
    Vertex(const DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT3 &normal, const DirectX::XMFLOAT2 &uv)
        : Position(position), Normal(normal)
    {
        UV[0] = uv;
    }

    DirectX::XMFLOAT3 Position;                           ///< Object-space position.
    DirectX::XMFLOAT3 Normal;                             ///< Object-space surface normal.
    DirectX::XMFLOAT2 UV[MaxTextureCoordinateSets] = {};  ///< Texture coordinates for each supported UV set.
    DirectX::XMFLOAT4 Tangent = {1.0f, 0.0f, 0.0f, 1.0f}; ///< Tangent basis vector and handedness.
    DirectX::XMFLOAT4 Color = {1.0f, 1.0f, 1.0f, 1.0f};   ///< Linear vertex color multiplier.
};
} // namespace Engine::Renderer
