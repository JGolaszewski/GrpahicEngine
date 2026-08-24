#include "backends/D3D11Backend.h"

#include <iostream>
#include <nvrhi/d3d11.h>
#include <nvrhi/validation.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>

namespace Engine {

	class NvrhiMessageCallback final : public nvrhi::IMessageCallback {
	public:
		void message(nvrhi::MessageSeverity severity, const char* messageText) override {
			const char* prefix = "INFO";
			switch(severity) {
				case nvrhi::MessageSeverity::Warning:
					prefix = "WARN";
					break;
				case nvrhi::MessageSeverity::Error:
					prefix = "ERROR";
					break;
				default:
					break;
			}

			std::cerr << "NVRHI " << prefix << ": " << (messageText ? messageText : "<null>") << std::endl;
		}
	};

	static NvrhiMessageCallback s_NvrhiMessageCallback;
	
	bool D3D11Backend::Initialize(IWindow* window) {
		HWND hwnd = static_cast<HWND>(window->GetNativeHandle());
		
		if(!hwnd) {
			std::cerr << "ERROR: Window HWND couldn't be obtained." << std::endl;
			return false;
		}

		
		UINT createDeviceFlags = 0;
	
		// #ifdef _DEBUG
		// 	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
		// #endif

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
		deviceDesc.messageCallback = &s_NvrhiMessageCallback;

		m_NvrhiDevice = nvrhi::d3d11::createDevice(deviceDesc);

		// #ifdef _DEBUG
		// if (m_NvrhiDevice)
		// {
		// 	  m_NvrhiDevice = nvrhi::validation::createValidationLayer(m_NvrhiDevice);
		// }
		// #endif

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

		nvrhi::TextureDesc depthDesc;
		depthDesc.width = width;
		depthDesc.height = height;
		depthDesc.format = nvrhi::Format::D24S8;
		depthDesc.isRenderTarget = true;
     depthDesc.isShaderResource = false;
		depthDesc.initialState = nvrhi::ResourceStates::DepthWrite;
		depthDesc.keepInitialState = true;
		depthDesc.debugName = "DepthBuffer";

		m_NvrhiDepthBuffer = m_NvrhiDevice->createTexture(depthDesc);

		if(!m_NvrhiDepthBuffer) {
			std::cerr << "ERROR: Failed to create NVRHI depth texture." << std::endl;
			return false;
		}

		nvrhi::FramebufferDesc framebufferDesc;
		framebufferDesc.addColorAttachment(m_NvrhiBackBuffer);
     framebufferDesc.setDepthAttachment(m_NvrhiDepthBuffer);
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
       if(!m_SwapChain || !m_Device || !m_NvrhiDevice || width <= 0 || height <= 0) {
			return;
		}

		m_Context->OMSetRenderTargets(0, nullptr, nullptr);

		m_NvrhiFramebuffer = nullptr;
		m_NvrhiBackBuffer = nullptr;
       m_NvrhiDepthBuffer = nullptr;
		m_RenderTargetView = nullptr;

		HRESULT hr = m_SwapChain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
		if(FAILED(hr)) {
			std::cerr << "ERROR: SwapChain resize failed." << std::endl;
			return;
		}

		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
		if(FAILED(hr) || !backBuffer) {
			std::cerr << "ERROR: Could not get backbuffer after resize." << std::endl;
			return;
		}

		hr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_RenderTargetView);
		if(FAILED(hr)) {
			std::cerr << "ERROR: Could not create RTV after resize." << std::endl;
			return;
		}

		nvrhi::TextureDesc textureDesc;
		textureDesc.width = static_cast<uint32_t>(width);
		textureDesc.height = static_cast<uint32_t>(height);
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
			std::cerr << "ERROR: Failed to wrap resized backbuffer into NVRHI texture." << std::endl;
			return;
		}

		nvrhi::TextureDesc depthDesc;
		depthDesc.width = static_cast<uint32_t>(width);
		depthDesc.height = static_cast<uint32_t>(height);
		depthDesc.format = nvrhi::Format::D24S8;
		depthDesc.isRenderTarget = true;
     depthDesc.isShaderResource = false;
		depthDesc.initialState = nvrhi::ResourceStates::DepthWrite;
		depthDesc.keepInitialState = true;
		depthDesc.debugName = "DepthBuffer";

		m_NvrhiDepthBuffer = m_NvrhiDevice->createTexture(depthDesc);

		if(!m_NvrhiDepthBuffer) {
			std::cerr << "ERROR: Failed to create resized NVRHI depth texture." << std::endl;
			return;
		}

		nvrhi::FramebufferDesc framebufferDesc;
		framebufferDesc.addColorAttachment(m_NvrhiBackBuffer);
     framebufferDesc.setDepthAttachment(m_NvrhiDepthBuffer);
		m_NvrhiFramebuffer = m_NvrhiDevice->createFramebuffer(framebufferDesc);

		if(!m_NvrhiFramebuffer) {
			std::cerr << "ERROR: Failed to create NVRHI framebuffer after resize." << std::endl;
		}
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