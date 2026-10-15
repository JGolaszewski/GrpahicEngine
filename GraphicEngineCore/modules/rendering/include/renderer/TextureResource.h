#pragma once

#include "runtime/include/CoreExport.h"

#include <memory>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{
class TextureBackendAccess;

/// Backend-owned texture, shader-resource view, and sampler bundle.
struct ENGINE_API TextureResource
{
    friend class TextureBackendAccess;

  public:
    /// Creates an empty texture resource for backend initialization.
    TextureResource();

    /// Releases the private backend texture and view resources.
    ~TextureResource();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
} // namespace Engine::Renderer

#ifdef _MSC_VER
#pragma warning(pop)
#endif
