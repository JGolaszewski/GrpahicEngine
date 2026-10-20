#pragma once

#include "runtime/include/CoreExport.h"

#include <cstdint>
#include <memory>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine {

class RenderSystem;
class Viewport;
class IRenderBackend;
namespace Renderer {
class RenderView;
}

/// High-level render target for an editor or runtime viewport.
/// Backend graphics resources remain opaque to callers.
class ENGINE_API ViewportTarget {
public:
  /// Releases the backend target resources.
  ~ViewportTarget();

  /// Render targets are uniquely owned and cannot be copied.
  ViewportTarget(const ViewportTarget&) = delete;
  ViewportTarget& operator=(const ViewportTarget&) = delete;

  /// Transfers ownership of the target resources.
  ViewportTarget(ViewportTarget&&) noexcept;

  /// Replaces this target with another target's resources.
  /// @param other Target whose resources are moved into this instance.
  /// @return Reference to this target.
  ViewportTarget& operator=(ViewportTarget&& other) noexcept;

  /// Reports whether backend framebuffer and color resources are valid.
  /// @return True when the target can be rendered into.
  bool IsValid() const noexcept;

  /// Returns the target width in pixels.
  /// @return Target width.
  uint32_t GetWidth() const noexcept;

  /// Returns the target height in pixels.
  /// @return Target height.
  uint32_t GetHeight() const noexcept;

private:
  struct Impl;

  ViewportTarget(void* device, uint32_t width, uint32_t height, bool finalOutput = false, bool snapshotOutput = false);
  void* GetColorTextureHandle() const noexcept;
  void* GetFramebufferHandle() const noexcept;
  void* GetSceneFramebufferHandle() const noexcept;
  void* GetOutputTextureHandle(uint32_t index) const noexcept;
  void* GetOverdrawTextureHandle() const noexcept;
  void* GetSnapshotOverflowBufferHandle() const noexcept;
  void* GetOverdrawFramebufferHandle() const noexcept;

  std::unique_ptr<Impl> m_Impl;

  friend class RenderSystem;
  friend class Viewport;
  friend class IRenderBackend;
  friend class Renderer::RenderView;
};
} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
