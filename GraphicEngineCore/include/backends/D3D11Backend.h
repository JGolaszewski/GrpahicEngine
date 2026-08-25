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

namespace Engine {

	class ENGINE_API D3D11Backend : public IRenderBackend {
	public:
		bool Initialize(IWindow* window) override;

		void Present() override;
		void Resize(int width, int height) override;

		void InitImGui(IWindow* window) override;
		void BeginImGuiFrame() override;
		void RenderImGui() override;
		void ShutdownImGui() override;
		void BeginGpuEvent(const wchar_t* name) override;
		void EndGpuEvent() override;

		void SetVSyncEnabled(bool enabled) noexcept { m_VSyncEnabled = enabled; }
		bool IsVSyncEnabled() const noexcept { return m_VSyncEnabled; }

		inline nvrhi::IDevice* GetDevice() const override {
			return m_NvrhiDevice.Get();
		}

		inline nvrhi::IFramebuffer* GetDefaultFramebuffer() const override {
			return m_NvrhiFramebuffer.Get();
		}

	private:
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
	};

}
