#include "backends/D3D11Backend.h"

#include <iostream>
#include <iomanip>
#include <nvrhi/d3d11.h>
#include <nvrhi/validation.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>

namespace Engine {

	class NvrhiMessageCallback final : public nvrhi::IMessageCallback {
	public:
		void message(nvrhi::MessageSeverity severity, const char* messageText) override {
			const char* prefix = "INFO";
			switch (severity) {
				case nvrhi::MessageSeverity::Warning: prefix = "WARN"; break;
				case nvrhi::MessageSeverity::Error: prefix = "ERROR"; break;
				default: break;
			}
			std::cerr << "NVRHI " << prefix << ": " << (messageText ? messageText : "<null>") << std::endl;
		}
	};

	static NvrhiMessageCallback s_NvrhiMessageCallback;

	static const char* FeatureLevelName(D3D_FEATURE_LEVEL featureLevel) {
		switch (featureLevel) {
			case D3D_FEATURE_LEVEL_11_1: return "11.1";
			case D3D_FEATURE_LEVEL_11_0: return "11.0";
			case D3D_FEATURE_LEVEL_10_1: return "10.1";
			case D3D_FEATURE_LEVEL_10_0: return "10.0";
			default: return "unknown";
		}
	}

	bool D3D11Backend::Initialize(IWindow* window) {
		if (!window) {
			std::cerr << "ERROR: D3D11 initialization received a null window." << std::endl;
			return false;
		}

		HWND hwnd = static_cast<HWND>(window->GetNativeHandle());
		if (!hwnd) {
			std::cerr << "ERROR: Window HWND couldn't be obtained." << std::endl;
			return false;
		}

		UINT createDeviceFlags = 0;
	#ifdef _DEBUG
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	#endif

		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.OutputWindow = hwnd;
		swapChainDesc.Windowed = TRUE;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
		HRESULT hr = D3D11CreateDeviceAndSwapChain(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
			nullptr, 0, D3D11_SDK_VERSION, &swapChainDesc, &m_SwapChain,
			&m_Device, &featureLevel, &m_Context);

	#ifdef _DEBUG
		if (FAILED(hr) && createDeviceFlags != 0) {
			std::cerr << "WARN: D3D11 debug layer unavailable; retrying without it." << std::endl;
			createDeviceFlags = 0;
			hr = D3D11CreateDeviceAndSwapChain(
				nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
				nullptr, 0, D3D11_SDK_VERSION, &swapChainDesc, &m_SwapChain,
				&m_Device, &featureLevel, &m_Context);
		}
	#endif

		if (FAILED(hr) || !m_Device || !m_Context || !m_SwapChain) {
			std::cerr << "ERROR: D3D11CreateDeviceAndSwapChain failed (HRESULT 0x"
				<< std::hex << static_cast<unsigned long>(hr) << std::dec << ")." << std::endl;
			return false;
		}

		m_Context.As(&m_Annotation);

		nvrhi::d3d11::DeviceDesc deviceDesc;
		deviceDesc.context = m_Context.Get();
		deviceDesc.messageCallback = &s_NvrhiMessageCallback;
		m_NvrhiDevice = nvrhi::d3d11::createDevice(deviceDesc);

	#ifdef _DEBUG
		if (m_NvrhiDevice) {
			m_NvrhiDevice = nvrhi::validation::createValidationLayer(m_NvrhiDevice);
		}
	#endif

		if (!m_NvrhiDevice) {
			std::cerr << "ERROR: NVRHI could not wrap the D3D11 device." << std::endl;
			return false;
		}

		const uint32_t width = window->GetWidth();
		const uint32_t height = window->GetHeight();
		if (!CreateSwapChainResources(width, height)) {
			return false;
		}

		std::cout << "INFO: DirectX 11 initialization succeeded (feature level "
			<< FeatureLevelName(featureLevel) << ", swap chain " << width << "x" << height << ")" << std::endl;
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		DXGI_ADAPTER_DESC adapterDesc{};
		if (SUCCEEDED(m_Device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
			SUCCEEDED(adapter->GetDesc(&adapterDesc))) {
			std::wcout << L"INFO: Adapter: " << adapterDesc.Description
				<< L" (vendor 0x" << std::hex << adapterDesc.VendorId
				<< L", device 0x" << adapterDesc.DeviceId << std::dec << L")" << std::endl;
		}
		return true;
	}

	bool D3D11Backend::CreateSwapChainResources(uint32_t width, uint32_t height) {
		if (!m_Device || !m_SwapChain || !m_NvrhiDevice || width == 0 || height == 0) {
			std::cerr << "ERROR: Cannot create swap-chain resources for invalid device or size." << std::endl;
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		HRESULT hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
		if (FAILED(hr) || !backBuffer) {
			std::cerr << "ERROR: GetBuffer failed (HRESULT 0x" << std::hex
				<< static_cast<unsigned long>(hr) << std::dec << ")." << std::endl;
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView;
		hr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView);
		if (FAILED(hr) || !renderTargetView) {
			std::cerr << "ERROR: CreateRenderTargetView failed (HRESULT 0x" << std::hex
				<< static_cast<unsigned long>(hr) << std::dec << ")." << std::endl;
			return false;
		}

		nvrhi::TextureDesc colorDesc;
		colorDesc.width = width;
		colorDesc.height = height;
		colorDesc.format = nvrhi::Format::RGBA8_UNORM;
		colorDesc.isRenderTarget = true;
		colorDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		colorDesc.keepInitialState = true;
		colorDesc.debugName = "BackBuffer";
		nvrhi::TextureHandle backBufferHandle = m_NvrhiDevice->createHandleForNativeTexture(
			nvrhi::ObjectTypes::D3D11_Resource, nvrhi::Object(backBuffer.Get()), colorDesc);
		if (!backBufferHandle) {
			std::cerr << "ERROR: Failed to wrap the swap-chain back buffer in NVRHI." << std::endl;
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
		nvrhi::TextureHandle depthBufferHandle = m_NvrhiDevice->createTexture(depthDesc);
		if (!depthBufferHandle) {
			std::cerr << "ERROR: Failed to create the D3D11 depth buffer." << std::endl;
			return false;
		}

		nvrhi::FramebufferDesc framebufferDesc;
		framebufferDesc.addColorAttachment(backBufferHandle);
		framebufferDesc.setDepthAttachment(depthBufferHandle);
		nvrhi::FramebufferHandle framebuffer = m_NvrhiDevice->createFramebuffer(framebufferDesc);
		if (!framebuffer) {
			std::cerr << "ERROR: Failed to create the D3D11 framebuffer." << std::endl;
			return false;
		}

		m_RenderTargetView = std::move(renderTargetView);
		m_NvrhiBackBuffer = std::move(backBufferHandle);
		m_NvrhiDepthBuffer = std::move(depthBufferHandle);
		m_NvrhiFramebuffer = std::move(framebuffer);
		m_SwapChainWidth = width;
		m_SwapChainHeight = height;
		return true;
	}

	void D3D11Backend::Present() {
		if (!m_SwapChain) return;
		const HRESULT hr = m_SwapChain->Present(m_VSyncEnabled ? 1 : 0, 0);
		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
			std::cerr << "ERROR: Present failed because the D3D11 device was removed or reset (HRESULT 0x"
				<< std::hex << static_cast<unsigned long>(hr) << std::dec << ")." << std::endl;
		}
		else if (FAILED(hr)) {
			std::cerr << "ERROR: Swap-chain Present failed (HRESULT 0x"
				<< std::hex << static_cast<unsigned long>(hr) << std::dec << ")." << std::endl;
		}
	}

	void D3D11Backend::BeginGpuEvent(const wchar_t* name) {
		if (m_Annotation.Get() && name) m_Annotation->BeginEvent(name);
	}

	void D3D11Backend::EndGpuEvent() {
		if (m_Annotation.Get()) m_Annotation->EndEvent();
	}

	void D3D11Backend::Resize(int width, int height) {
		if (!m_SwapChain || !m_Device || !m_NvrhiDevice || width <= 0 || height <= 0) return;

		const uint32_t previousWidth = m_SwapChainWidth;
		const uint32_t previousHeight = m_SwapChainHeight;
		m_Context->OMSetRenderTargets(0, nullptr, nullptr);
		m_NvrhiFramebuffer = nullptr;
		m_NvrhiBackBuffer = nullptr;
		m_NvrhiDepthBuffer = nullptr;
		m_RenderTargetView.Reset();

		HRESULT hr = m_SwapChain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
		if (FAILED(hr)) {
			std::cerr << "ERROR: ResizeBuffers failed (HRESULT 0x" << std::hex
				<< static_cast<unsigned long>(hr) << std::dec << "); restoring the previous framebuffer." << std::endl;
			if (previousWidth > 0 && previousHeight > 0 && !CreateSwapChainResources(previousWidth, previousHeight)) {
				std::cerr << "ERROR: Could not restore the previous framebuffer after resize failure." << std::endl;
			}
			return;
		}

		if (CreateSwapChainResources(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
			return;
		}

		std::cerr << "ERROR: New swap-chain resources were incomplete; attempting to restore the previous size." << std::endl;
		m_NvrhiFramebuffer = nullptr;
		m_NvrhiBackBuffer = nullptr;
		m_NvrhiDepthBuffer = nullptr;
		m_RenderTargetView.Reset();
		if (previousWidth > 0 && previousHeight > 0) {
			hr = m_SwapChain->ResizeBuffers(0, previousWidth, previousHeight, DXGI_FORMAT_UNKNOWN, 0);
			if (SUCCEEDED(hr) && CreateSwapChainResources(previousWidth, previousHeight)) {
				return;
			}
		}
		std::cerr << "ERROR: Swap-chain restore failed; rendering is unavailable until the device is recreated." << std::endl;
	}

	void D3D11Backend::InitImGui(IWindow* window) {
		ImGui_ImplDX11_Init(m_Device.Get(), m_Context.Get());
	}

	void D3D11Backend::BeginImGuiFrame() {
		ImGui_ImplDX11_NewFrame();
	}

	void D3D11Backend::RenderImGui() {
		if (!m_RenderTargetView) return;
		m_Context->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), nullptr);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}

	void D3D11Backend::ShutdownImGui() {
		ImGui_ImplDX11_Shutdown();
	}

}
