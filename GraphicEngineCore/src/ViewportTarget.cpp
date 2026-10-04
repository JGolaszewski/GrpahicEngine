#include "ViewportTarget.h"

#include <d3d11.h>
#include <nvrhi/nvrhi.h>
#include <wrl/client.h>

namespace Engine {

    struct ViewportTarget::Impl {
        uint32_t Width = 0;
        uint32_t Height = 0;

        nvrhi::TextureHandle ColorTexture;
        nvrhi::TextureHandle DepthTexture;
        nvrhi::FramebufferHandle Framebuffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ColorSRV;
    };

    ViewportTarget::ViewportTarget(void* device, uint32_t width, uint32_t height)
        : m_Impl(std::make_unique<Impl>()) {
        m_Impl->Width = width;
        m_Impl->Height = height;

        if (!device || width == 0 || height == 0) {
            return;
        }

        auto* nvrhiDevice = static_cast<nvrhi::IDevice*>(device);

        nvrhi::TextureDesc colorDesc;
        colorDesc.width = width;
        colorDesc.height = height;
        colorDesc.format = nvrhi::Format::RGBA8_UNORM;
        colorDesc.isRenderTarget = true;
        colorDesc.isShaderResource = true;
        colorDesc.initialState = nvrhi::ResourceStates::RenderTarget;
        colorDesc.keepInitialState = true;
        colorDesc.debugName = "ViewportColor";
        m_Impl->ColorTexture = nvrhiDevice->createTexture(colorDesc);
        if (!m_Impl->ColorTexture) {
            return;
        }

        nvrhi::TextureDesc depthDesc;
        depthDesc.width = width;
        depthDesc.height = height;
        depthDesc.depth = 1;
        depthDesc.arraySize = 1;
        depthDesc.mipLevels = 1;
        depthDesc.format = nvrhi::Format::D24S8;
        depthDesc.sampleCount = 1;
        depthDesc.sampleQuality = 0;
        depthDesc.isRenderTarget = true;
        depthDesc.isUAV = false;
        depthDesc.isShaderResource = false;
        depthDesc.initialState = nvrhi::ResourceStates::DepthWrite;
        depthDesc.keepInitialState = true;
        depthDesc.debugName = "ViewportDepth";
        m_Impl->DepthTexture = nvrhiDevice->createTexture(depthDesc);
        if (!m_Impl->DepthTexture) {
            return;
        }

        nvrhi::FramebufferDesc framebufferDesc;
        framebufferDesc.addColorAttachment(m_Impl->ColorTexture);
        framebufferDesc.setDepthAttachment(m_Impl->DepthTexture);
        m_Impl->Framebuffer = nvrhiDevice->createFramebuffer(framebufferDesc);
        if (!m_Impl->Framebuffer) {
            return;
        }

        const nvrhi::Object nativeView = m_Impl->ColorTexture->getNativeView(
            nvrhi::ObjectTypes::D3D11_ShaderResourceView);
        m_Impl->ColorSRV = static_cast<ID3D11ShaderResourceView*>(nativeView);
    }

    ViewportTarget::~ViewportTarget() = default;
    ViewportTarget::ViewportTarget(ViewportTarget&&) noexcept = default;
    ViewportTarget& ViewportTarget::operator=(ViewportTarget&&) noexcept = default;

    bool ViewportTarget::IsValid() const noexcept {
        return m_Impl && m_Impl->ColorTexture && m_Impl->DepthTexture && m_Impl->Framebuffer && m_Impl->ColorSRV;
    }

    uint32_t ViewportTarget::GetWidth() const noexcept {
        return m_Impl ? m_Impl->Width : 0;
    }

    uint32_t ViewportTarget::GetHeight() const noexcept {
        return m_Impl ? m_Impl->Height : 0;
    }

    void* ViewportTarget::GetTextureId() const noexcept {
        return m_Impl ? static_cast<void*>(m_Impl->ColorSRV.Get()) : nullptr;
    }

    void* ViewportTarget::GetFramebufferHandle() const noexcept {
        return m_Impl ? static_cast<void*>(m_Impl->Framebuffer.Get()) : nullptr;
    }
}
