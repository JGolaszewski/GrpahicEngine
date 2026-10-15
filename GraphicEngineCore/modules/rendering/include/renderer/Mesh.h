#pragma once

#include "runtime/include/CoreExport.h"
#include "rendering/include/renderer/RenderState.h"
#include "rendering/include/renderer/Vertex.h"

#include <cstdint>
#include <memory>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{
class MeshBackendAccess;
class RenderDevice;
class EntityBuilder;

/// GPU geometry buffers and topology metadata for a renderable mesh.
class ENGINE_API Mesh
{
    friend class MeshBackendAccess;
    friend class RenderDevice;
    friend class EntityBuilder;

  public:
    /// Releases the backend vertex and index buffers.
    ~Mesh();

    /// Returns the number of vertices stored in the GPU buffer.
    /// @return Vertex count.
    uint32_t GetVertexCount() const noexcept;

    /// Returns the number of indices stored in the GPU buffer.
    /// @return Index count, or zero when the mesh is not indexed.
    uint32_t GetIndexCount() const noexcept;

    /// Reports whether the mesh uses an index buffer.
    /// @return True when indices are present.
    bool HasIndices() const noexcept;

    /// Reports whether the backend buffers were created successfully.
    /// @return True when the mesh is ready for drawing.
    bool IsValid() const noexcept;

    /// Returns the primitive assembly mode for this mesh.
    /// @return Mesh topology.
    PrimitiveTopology GetPrimitiveTopology() const noexcept;

  private:
    struct Impl;
    Mesh();

    std::unique_ptr<Impl> m_Impl;
};
} // namespace Engine::Renderer

#ifdef _MSC_VER
#pragma warning(pop)
#endif
