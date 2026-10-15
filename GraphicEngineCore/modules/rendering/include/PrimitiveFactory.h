#pragma once

#include "runtime/include/CoreExport.h"

#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <optional>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer {
class Material;
struct Submesh;
} // namespace Engine::Renderer

namespace Engine {

class RenderSystem;

/// Built-in mesh shapes that can be created by PrimitiveFactory.
enum class PrimitiveShape : uint8_t { Cube, Sphere, Plane, Cone };

/// Creates engine primitives and performs their GPU upload internally.
class ENGINE_API PrimitiveFactory {
public:
  ~PrimitiveFactory();

  PrimitiveFactory(const PrimitiveFactory&) = delete;
  PrimitiveFactory& operator=(const PrimitiveFactory&) = delete;
  /// Transfers ownership of the primitive factory implementation.
  PrimitiveFactory(PrimitiveFactory&&) noexcept;
  /// Replaces this factory with the implementation owned by another factory.
  /// @return This factory after assignment.
  PrimitiveFactory& operator=(PrimitiveFactory&&) noexcept;

  /// Creates and uploads one built-in primitive mesh.
  /// @param shape Shape to create.
  /// @param material Material assigned to the new submesh.
  /// @return The uploaded submesh, or no value if creation or upload fails.
  std::optional<::Engine::Renderer::Submesh> CreatePrimitive(PrimitiveShape shape,
                                                             std::shared_ptr<::Engine::Renderer::Material> material);

private:
  struct Impl;

  explicit PrimitiveFactory(void* device);

  std::unique_ptr<Impl> m_Impl;

  friend class RenderSystem;
};
} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
