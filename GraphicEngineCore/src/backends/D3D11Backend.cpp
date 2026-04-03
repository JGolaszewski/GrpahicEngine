#include "backends/D3D11Backend.h"

#include <iostream>
#include <nvrhi/d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>

namespace Engine {
	
	bool D3D11Backend::Initialize(IWindow* window) {
		HWND hwnd = static_cast<HWND>(window->GetNativeHandle());
		
		if(!hwnd) {
			std::cerr << "ERROR: Window HWND couldn't be obtained." << std::endl;
			return false;
		}

		
		UINT createDeviceFlags = 0;
	
		#ifdef _DEBUG
			createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
		#endif

		DXGI_SWAP_CHAIN_DESC sd;
		ZeroMemory(&sd, sizeof(sd));

		// Get window size from hwnd
		sd.BufferDesc.Width = 0;
		sd.BufferDesc.Height = 0;

		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;

		// Disable MSAA
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 2;
		sd.OutputWindow = hwnd;
		sd.Windowed = TRUE;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		D3D_FEATURE_LEVEL featureLevel;
		HRESULT hr = D3D11CreateDeviceAndSwapChain(
			nullptr,                    // Default GPU
			D3D_DRIVER_TYPE_HARDWARE,   // Use GPU
			nullptr,
			createDeviceFlags,
			nullptr, 0,                 // Default function levels
			D3D11_SDK_VERSION,
			&sd,
			&m_SwapChain,
			&m_Device,
			&featureLevel,
			&m_Context
		);

		if(FAILED(hr)) {
			std::cerr << "ERROR: Initialization failed." << std::endl;
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);

		m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_RenderTargetView);

		// Pass to nvrhi
		nvrhi::d3d11::DeviceDesc deviceDesc;
		deviceDesc.context = m_Context.Get();

		m_NvrhiDevice = nvrhi::d3d11::createDevice(deviceDesc);

		if(!m_NvrhiDevice) {
			std::cerr << "ERROR: Parsing DX11 device by NVRHI failed." << std::endl;
			return false;
		}

		uint32_t width = window->GetWidth();
		uint32_t height = window->GetHeight();

		nvrhi::TextureDesc textureDesc;
		textureDesc.width = width;
		textureDesc.height = height;
		textureDesc.format = nvrhi::Format::RGBA8_UNORM;
		textureDesc.isRenderTarget = true;
		textureDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		textureDesc.keepInitialState = true;
		textureDesc.debugName = "BackBuffer";

		m_NvrhiBackBuffer = m_NvrhiDevice->createHandleForNativeTexture(
			nvrhi::ObjectTypes::D3D11_Resource,
			nvrhi::Object(backBuffer.Get()),
			textureDesc
		);

		if(!m_NvrhiBackBuffer) {
			std::cerr << "ERROR: Failed to wrap DX11 BackBuffer into NVRHI Texture." << std::endl;
			return false;
		}

		nvrhi::FramebufferDesc framebufferDesc;
		framebufferDesc.addColorAttachment(m_NvrhiBackBuffer);
		m_NvrhiFramebuffer = m_NvrhiDevice->createFramebuffer(framebufferDesc);

		if(!m_NvrhiFramebuffer) {
			std::cerr << "ERROR: Failed to create NVRHI Framebuffer." << std::endl;
			return false;
		}

		std::cout << "INFO: DirectX 11 initialization succeeded" << std::endl;
		return true;
	}

	void D3D11Backend::Present() {
		if(m_SwapChain) {
			m_SwapChain->Present(0, 0);
		}
	}

	void D3D11Backend::Resize(int width, int height) {
		// TODO: Resize logic
	}

	void D3D11Backend::InitImGui(IWindow* window) {
		ImGui_ImplDX11_Init(m_Device.Get(), m_Context.Get());
	}

	void D3D11Backend::BeginImGuiFrame() {
		ImGui_ImplDX11_NewFrame();
	}

	void D3D11Backend::RenderImGui() {
		ImGui::Render();

		m_Context->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), nullptr);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}

}