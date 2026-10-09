#pragma once

#include "runtime/include/CoreExport.h"
#include "runtime/include/Window.h"

#include <cstdint>
#include <nvrhi/nvrhi.h>

namespace Engine
{

struct BackendDiagnostics
{
    const char *BackendName = "unknown";
    bool DeviceReady = false;
    bool VSyncEnabled = false;
    bool HasVSync = false;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t AdapterVendorId = 0;
    uint32_t AdapterDeviceId = 0;
    uint64_t DeviceGeneration = 0;
    uint64_t ValidationWarningCount = 0;
    uint64_t ValidationErrorCount = 0;
    int64_t LastPresentResult = 0;
    int64_t LastBackendError = 0;
    int64_t DeviceRemovalReason = 0;
    float LastPresentTimeMs = 0.0f;
    float LastGpuFrameTimeMs = 0.0f;
};

class ENGINE_API IRenderBackend
{
  public:
    virtual ~IRenderBackend() = default;

    virtual bool Initialize(IWindow *window) = 0;
    virtual void Present() = 0;
    virtual void Resize(int width, int height) = 0;

    virtual nvrhi::IDevice *GetDevice() const = 0;
    virtual nvrhi::IFramebuffer *GetDefaultFramebuffer() const = 0;
    virtual BackendDiagnostics GetDiagnostics() const noexcept
    {
        return {};
    }
    virtual bool SupportsVSyncControl() const noexcept
    {
        return false;
    }
    virtual bool SetVSyncEnabled(bool) noexcept
    {
        return false;
    }
    virtual void *RegisterViewportTexture(void *) const
    {
        return nullptr;
    }

    virtual void BeginGpuEvent(const wchar_t *)
    {
    }
    virtual void EndGpuEvent()
    {
    }
};
} // namespace Engine
