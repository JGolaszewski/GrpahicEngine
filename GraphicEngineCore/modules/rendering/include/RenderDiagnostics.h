#pragma once

#include <cstdint>

namespace Engine::Renderer {
/// Allocation and draw counters captured for one completed frame.
struct FrameDiagnostics {
  uint64_t UploadBytes = 0;           ///< Bytes uploaded to GPU resources during the frame.
  uint32_t BufferAllocations = 0;     ///< GPU buffers allocated during the frame.
  uint32_t BindingSetAllocations = 0; ///< Resource binding sets allocated during the frame.
  uint32_t DrawCalls = 0;             ///< Draw calls recorded during the frame.
};

/// Result of a non-blocking GPU frame-timing query.
struct GpuFrameTiming {
  bool Available = false;  ///< True when TimeMs contains a completed measurement.
  bool Pending = false;    ///< True when the query has not completed yet.
  uint64_t FrameIndex = 0; ///< Frame associated with the timing result.
  float TimeMs = 0.0f;     ///< GPU duration in milliseconds when Available is true.
};
} // namespace Engine::Renderer
