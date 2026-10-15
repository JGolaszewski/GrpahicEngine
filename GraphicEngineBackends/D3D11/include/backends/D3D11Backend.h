// Initialization from:
// https://www.braynzarsoft.net/viewtutorial/q16390-3-initializing-direct3d-11
//

#pragma once

#include "runtime/include/CoreExport.h"
#include "rendering/include/RenderBackend.h"

#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <nvrhi/nvrhi.h>
#include <wrl/client.h>

namespace Engine
{

class D3D11Backend : public IRenderBackend
{
  public:
    bool Initialize(IWindow *window) override;

    void Present() override;
    void Resize(int width, int height) override;
    BackendDiagnostics GetDiagnostics() const noexcept override;
    bool InitializeEditorUi(IWindow *window) override;
    void BeginEditorUiFrame() override;
    void RenderEditorUi(void *drawData) override;
    void ShutdownEditorUi() override;

    void BeginGpuEvent(const wchar_t *name) override;
    void EndGpuEvent() override;

    bool SupportsVSyncControl() const noexcept override
    {
        return true;
    }
    bool SetVSyncEnabled(bool enabled) noexcept override
    {
        m_VSyncEnabled = enabled;
        return true;
    }
    bool IsVSyncEnabled() const noexcept
    {
        return m_VSyncEnabled;
    }

    inline void *GetDevice() const override
    {
        return m_NvrhiDevice.Get();
    }

    inline void *GetDefaultFramebuffer() const override
    {
        return m_NvrhiFramebuffer.Get();
    }

  private:
    void *RegisterViewportTextureHandle(void *renderTexture) const override;
    bool CreateSwapChainResources(uint32_t width, uint32_t height);
    Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_Context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> m_Annotation;
    bool m_VSyncEnabled = true;
    HRESULT m_LastPresentFailure = S_OK;
    HRESULT m_LastPresentResult = S_OK;
    HRESULT m_LastBackendError = S_OK;
    uint32_t m_AdapterVendorId = 0;
    uint32_t m_AdapterDeviceId = 0;
    uint64_t m_DeviceGeneration = 0;
    float m_LastPresentTimeMs = 0.0f;

    nvrhi::DeviceHandle m_NvrhiDevice;
    nvrhi::TextureHandle m_NvrhiBackBuffer;
    nvrhi::TextureHandle m_NvrhiDepthBuffer;
    nvrhi::FramebufferHandle m_NvrhiFramebuffer;
    uint32_t m_SwapChainWidth = 0;
    uint32_t m_SwapChainHeight = 0;
};

} // namespace Engine
