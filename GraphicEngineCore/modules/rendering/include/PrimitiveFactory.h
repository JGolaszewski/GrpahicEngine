#pragma once

#include "runtime/include/CoreExport.h"

#include <DirectXMath.h>
#include <cstdint>
#include <memory>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{
class Entity;
class Material;
} // namespace Engine::Renderer

namespace Engine
{

class RenderSystem;

enum class PrimitiveShape : uint8_t
{
    Cube,
    Sphere,
    Plane,
    Cone
};

// Creates engine primitives and performs their GPU upload internally.
class ENGINE_API PrimitiveFactory
{
  public:
    ~PrimitiveFactory();

    PrimitiveFactory(const PrimitiveFactory &) = delete;
    PrimitiveFactory &operator=(const PrimitiveFactory &) = delete;
    PrimitiveFactory(PrimitiveFactory &&) noexcept;
    PrimitiveFactory &operator=(PrimitiveFactory &&) noexcept;

    std::shared_ptr<::Engine::Renderer::Entity> CreatePrimitive(PrimitiveShape shape,
                                                      std::shared_ptr<::Engine::Renderer::Material> material,
                                                      const DirectX::XMFLOAT3 &position,
                                                      const DirectX::XMFLOAT3 &scale = {1.0f, 1.0f, 1.0f});

  private:
    struct Impl;

    explicit PrimitiveFactory(void *device);

    std::unique_ptr<Impl> m_Impl;

    friend class RenderSystem;
};
} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
