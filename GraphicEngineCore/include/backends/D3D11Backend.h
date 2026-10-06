// Initialization from:
// https://www.braynzarsoft.net/viewtutorial/q16390-3-initializing-direct3d-11
//

#pragma once

#include "CoreExport.h"
#include "RenderBackend.h"

#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace Engine
{

class D3D11Backend : public IRenderBackend
{
  public:
    bool Initialize(IWindow *window) override;

    void Present() override;
    void Resize(int width, int height) override;

    void BeginGpuEvent(const wchar_t *name) override;
    void EndGpuEvent() override;

    ID3D11Device *GetNativeDevice() const noexcept
    {
        return m_Device.Get();
    }
    ID3D11DeviceContext *GetNativeContext() const noexcept
    {
        return m_Context.Get();
    }
    ID3D11RenderTargetView *GetBackBufferRenderTarget() const noexcept
    {
        return m_RenderTargetView.Get();
    }

    void SetVSyncEnabled(bool enabled) noexcept
    {
        m_VSyncEnabled = enabled;
    }
    bool IsVSyncEnabled() const noexcept
    {
        return m_VSyncEnabled;
    }

    inline nvrhi::IDevice *GetDevice() const override
    {
        return m_NvrhiDevice.Get();
    }

    inline nvrhi::IFramebuffer *GetDefaultFramebuffer() const override
    {
        return m_NvrhiFramebuffer.Get();
    }
    void *RegisterViewportTexture(void *renderTexture) const override;

  private:
    bool CreateSwapChainResources(uint32_t width, uint32_t height);
    Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_Context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> m_Annotation;
    bool m_VSyncEnabled = true;

    nvrhi::DeviceHandle m_NvrhiDevice;
    nvrhi::TextureHandle m_NvrhiBackBuffer;
    nvrhi::TextureHandle m_NvrhiDepthBuffer;
    nvrhi::FramebufferHandle m_NvrhiFramebuffer;
    uint32_t m_SwapChainWidth = 0;
    uint32_t m_SwapChainHeight = 0;
};

} // namespace Engine
