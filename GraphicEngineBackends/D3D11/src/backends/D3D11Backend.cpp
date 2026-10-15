#include "backends/D3D11Backend.h"
#include "runtime/include/LogEngine.h"
#include "runtime/include/LogMacros.h"

#include <atomic>
#include <chrono>
#include <nvrhi/d3d11.h>
#include <nvrhi/validation.h>

namespace Engine {
namespace {
auto ValidationWarnings() -> std::atomic<uint64_t>& {
  static std::atomic<uint64_t> count{0};
  return count;
}

auto ValidationErrors() -> std::atomic<uint64_t>& {
  static std::atomic<uint64_t> count{0};
  return count;
}
} // namespace

auto D3D11Backend::RegisterViewportTexture(void* renderTexture) const -> void* {
  auto* texture = static_cast<nvrhi::ITexture*>(renderTexture);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  return (texture != nullptr) ? texture->getNativeView(nvrhi::ObjectTypes::D3D11_ShaderResourceView).pointer : nullptr;
}

class NvrhiMessageCallback final : public nvrhi::IMessageCallback {
public:
  void message(nvrhi::MessageSeverity severity, const char* messageText) override {
    switch (severity) {
    case nvrhi::MessageSeverity::Warning:
      ValidationWarnings().fetch_add(1, std::memory_order_relaxed);
      ENGINE_LOG_WARN("Backend", "NVRHI diagnostic | severity: Warning | message: {}",
                      messageText ? messageText : "<null>");
      break;
    case nvrhi::MessageSeverity::Error:
      ValidationErrors().fetch_add(1, std::memory_order_relaxed);
      ENGINE_LOG_ERROR_CODE("Backend", "backend.nvrhi.validation_error",
                            "NVRHI diagnostic | severity: Error | message: {}", messageText ? messageText : "<null>");
      break;
    default:
      break;
    }
  }
};

auto GetNvrhiMessageCallback() -> NvrhiMessageCallback& {
  static NvrhiMessageCallback callback;
  return callback;
}

static auto FeatureLevelName(D3D_FEATURE_LEVEL featureLevel) -> const char* {
  switch (featureLevel) {
  case D3D_FEATURE_LEVEL_11_1:
    return "11.1";
  case D3D_FEATURE_LEVEL_11_0:
    return "11.0";
  case D3D_FEATURE_LEVEL_10_1:
    return "10.1";
  case D3D_FEATURE_LEVEL_10_0:
    return "10.0";
  default:
    return "unknown";
  }
}

auto D3D11Backend::Initialize(IWindow* window) -> bool {
  Engine::Log::ScopedOperation operation;
  ENGINE_LOG_INFO("Render", "D3D11 backend initialization started | window_valid: {}", window != nullptr);
  if (window == nullptr) {
    ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.initialize.null_window",
                          "D3D11 initialization rejected | reason: null_window");
    return false;
  }

  HWND hwnd = static_cast<HWND>(window->GetNativeHandle());
  if (hwnd == nullptr) {
    ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.initialize.null_native_handle",
                          "D3D11 initialization failed | native_window_handle: 0");
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
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, nullptr, 0,
                                             D3D11_SDK_VERSION, &swapChainDesc, &m_SwapChain, &m_Device, &featureLevel,
                                             &m_Context);

#ifdef _DEBUG
  if (FAILED(hr) && createDeviceFlags != 0) {
    ENGINE_LOG_WARN("Render", "D3D11 debug device creation failed | hresult: 0x{:X} | retry: without_debug_layer",
                    static_cast<uint32_t>(hr));
    createDeviceFlags = 0;
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, nullptr, 0,
                                       D3D11_SDK_VERSION, &swapChainDesc, &m_SwapChain, &m_Device, &featureLevel,
                                       &m_Context);
  }
#endif

  if (FAILED(hr) || (m_Device == nullptr) || (m_Context == nullptr) || (m_SwapChain == nullptr)) {
    ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.device.create_failed",
                          "D3D11 device and swap-chain creation failed | hresult: 0x{:X} | device_valid: {} | "
                          "context_valid: {} | swap_chain_valid: {}",
                          static_cast<uint32_t>(hr), m_Device.Get() != nullptr, m_Context.Get() != nullptr,
                          m_SwapChain.Get() != nullptr);
    return false;
  }

  m_Context.As(&m_Annotation);

  nvrhi::d3d11::DeviceDesc deviceDesc;
  deviceDesc.context = m_Context.Get();
  deviceDesc.messageCallback = &GetNvrhiMessageCallback();
  m_NvrhiDevice = nvrhi::d3d11::createDevice(deviceDesc);

#ifdef _DEBUG
  if (m_NvrhiDevice != nullptr) {
    m_NvrhiDevice = nvrhi::validation::createValidationLayer(m_NvrhiDevice);
  }
#endif

  if (m_NvrhiDevice == nullptr) {
    ENGINE_LOG_ERROR_CODE("Backend", "backend.nvrhi.device.create_failed",
                          "NVRHI D3D11 device creation failed | d3d_device_valid: {}", m_Device != nullptr);
    return false;
  }

  const uint32_t width = window->GetWidth();
  const uint32_t height = window->GetHeight();
  if (!CreateSwapChainResources(width, height)) {
    ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.swap_chain.initialize_failed",
                          "D3D11 swap-chain resource initialization failed | width: {} | height: {}", width, height);
    return false;
  }

  ENGINE_LOG_INFO("Render", "D3D11 backend initialized | feature_level: {} | width: {} | height: {}",
                  FeatureLevelName(featureLevel), width, height);
  Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC adapterDesc{};
  if (SUCCEEDED(m_Device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
      SUCCEEDED(adapter->GetDesc(&adapterDesc))) {
    m_AdapterVendorId = adapterDesc.VendorId;
    m_AdapterDeviceId = adapterDesc.DeviceId;
    ENGINE_LOG_INFO("Render", "D3D11 adapter detected | vendor_id: 0x{:X} | device_id: 0x{:X}", adapterDesc.VendorId,
                    adapterDesc.DeviceId);
  }
  ++m_DeviceGeneration;
  return true;
}

auto D3D11Backend::CreateSwapChainResources(uint32_t width, uint32_t height) -> bool {
  if ((m_Device == nullptr) || (m_SwapChain == nullptr) || (m_NvrhiDevice == nullptr) || width == 0 || height == 0) {
    ENGINE_LOG_ERROR_CODE(
        "Backend", "backend.d3d11.swap_chain.resource_rejected",
        "Swap-chain resource creation rejected | device_valid: {} | swap_chain_valid: {} | width: {} | height: {}",
        m_Device != nullptr, m_SwapChain != nullptr, width, height);
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
  HRESULT hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
  if (FAILED(hr) || (backBuffer == nullptr)) {
    ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.swap_chain.back_buffer_failed",
                          "Swap-chain back-buffer acquisition failed | hresult: 0x{:X} | buffer_index: 0",
                          static_cast<uint32_t>(hr));
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView;
  hr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView);
  if (FAILED(hr) || (renderTargetView == nullptr)) {
    ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.render_target.create_failed",
                          "Render-target view creation failed | hresult: 0x{:X} | width: {} | height: {}",
                          static_cast<uint32_t>(hr), width, height);
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
  if (backBufferHandle == nullptr) {
    ENGINE_LOG_ERROR_CODE("Backend", "backend.nvrhi.back_buffer.wrap_failed",
                          "NVRHI back-buffer wrapping failed | width: {} | height: {}", width, height);
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
  if (depthBufferHandle == nullptr) {
    ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.depth_buffer.create_failed",
                          "D3D11 depth-buffer creation failed | width: {} | height: {} | format: D24S8", width, height);
    return false;
  }

  nvrhi::FramebufferDesc framebufferDesc;
  framebufferDesc.addColorAttachment(backBufferHandle);
  framebufferDesc.setDepthAttachment(depthBufferHandle);
  nvrhi::FramebufferHandle framebuffer = m_NvrhiDevice->createFramebuffer(framebufferDesc);
  if (framebuffer == nullptr) {
    ENGINE_LOG_ERROR_CODE("Backend", "backend.nvrhi.framebuffer.create_failed",
                          "D3D11 framebuffer creation failed | color_valid: {} | depth_valid: {}",
                          backBufferHandle != nullptr, depthBufferHandle != nullptr);
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
  if (m_SwapChain == nullptr) {
    m_LastPresentTimeMs = 0.0F;
    return;
  }
  const auto presentStart = std::chrono::steady_clock::now();
  const HRESULT hr = m_SwapChain->Present(m_VSyncEnabled ? 1 : 0, 0);
  m_LastPresentTimeMs =
      std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - presentStart).count();
  m_LastPresentResult = hr;
  if (SUCCEEDED(hr)) {
    m_LastPresentFailure = S_OK;
    return;
  }
  if (hr == m_LastPresentFailure) {
    return;
  }
  m_LastPresentFailure = hr;
  m_LastBackendError = hr;
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    Engine::Log::ScopedOperation operation;
    const HRESULT removalReason = (m_Device != nullptr) ? m_Device->GetDeviceRemovedReason() : E_FAIL;
    ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.present.device_removed",
                          "D3D11 device removed during present | hresult: 0x{:X} | device_removed_reason: 0x{:X} | "
                          "width: {} | height: {} | vsync: {}",
                          static_cast<uint32_t>(hr), static_cast<uint32_t>(removalReason), m_SwapChainWidth,
                          m_SwapChainHeight, m_VSyncEnabled);
  } else {
    Engine::Log::ScopedOperation operation;
    ENGINE_LOG_ERROR_CODE(
        "Backend", "backend.d3d11.present.failed",
        "D3D11 swap-chain present failed | hresult: 0x{:X} | width: {} | height: {} | vsync: {} | present_ms: {:.3f}",
        static_cast<uint32_t>(hr), m_SwapChainWidth, m_SwapChainHeight, m_VSyncEnabled, m_LastPresentTimeMs);
  }
}

auto D3D11Backend::GetDiagnostics() const noexcept -> BackendDiagnostics {
  BackendDiagnostics diagnostics;
  diagnostics.BackendName = "D3D11 / NVRHI";
  diagnostics.DeviceReady = m_Device != nullptr && m_NvrhiDevice != nullptr;
  diagnostics.HasVSync = true;
  diagnostics.VSyncEnabled = m_VSyncEnabled;
  diagnostics.Width = m_SwapChainWidth;
  diagnostics.Height = m_SwapChainHeight;
  diagnostics.AdapterVendorId = m_AdapterVendorId;
  diagnostics.AdapterDeviceId = m_AdapterDeviceId;
  diagnostics.DeviceGeneration = m_DeviceGeneration;
  diagnostics.ValidationWarningCount = ValidationWarnings().load(std::memory_order_relaxed);
  diagnostics.ValidationErrorCount = ValidationErrors().load(std::memory_order_relaxed);
  diagnostics.LastPresentResult = static_cast<int64_t>(m_LastPresentResult);
  diagnostics.LastBackendError = static_cast<int64_t>(m_LastBackendError);
  diagnostics.DeviceRemovalReason =
      (m_Device != nullptr) ? static_cast<int64_t>(m_Device->GetDeviceRemovedReason()) : 0;
  diagnostics.LastPresentTimeMs = m_LastPresentTimeMs;
  return diagnostics;
}

void D3D11Backend::BeginGpuEvent(const wchar_t* name) {
  if ((m_Annotation.Get() != nullptr) && (name != nullptr)) {
    m_Annotation->BeginEvent(name);
  }
}

void D3D11Backend::EndGpuEvent() {
  if (m_Annotation.Get() != nullptr) {
    m_Annotation->EndEvent();
  }
}

void D3D11Backend::Resize(int width, int height) {
  Engine::Log::ScopedOperation operation;
  if ((m_SwapChain == nullptr) || (m_Device == nullptr) || (m_NvrhiDevice == nullptr) || width <= 0 || height <= 0) {
    return;
  }

  const uint32_t previousWidth = m_SwapChainWidth;
  const uint32_t previousHeight = m_SwapChainHeight;
  m_Context->OMSetRenderTargets(0, nullptr, nullptr);
  m_NvrhiFramebuffer = nullptr;
  m_NvrhiBackBuffer = nullptr;
  m_NvrhiDepthBuffer = nullptr;
  m_RenderTargetView.Reset();

  HRESULT hr =
      m_SwapChain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr)) {
    m_LastBackendError = hr;
    ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.resize.failed",
                          "Swap-chain resize failed | hresult: 0x{:X} | requested_width: {} | requested_height: {} | "
                          "previous_width: {} | previous_height: {}",
                          static_cast<uint32_t>(hr), width, height, previousWidth, previousHeight);
    if (previousWidth > 0 && previousHeight > 0 && !CreateSwapChainResources(previousWidth, previousHeight)) {
      ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.resize.restore_failed",
                            "Previous swap-chain framebuffer restoration failed | width: {} | height: {}",
                            previousWidth, previousHeight);
    }
    return;
  }

  if (CreateSwapChainResources(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
    ENGINE_LOG_DEBUG("Render", "Swap-chain resized | width: {} | height: {}", width, height);
    return;
  }

  ENGINE_LOG_ERROR_CODE(
      "Backend", "backend.d3d11.resize.resources_incomplete",
      "Resized swap-chain resources incomplete | requested_width: {} | requested_height: {} | recovery: previous_size",
      width, height);
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
  ENGINE_LOG_ERROR_CODE("Backend", "backend.d3d11.resize.recovery_failed",
                        "Swap-chain recovery failed | requested_width: {} | requested_height: {} | previous_width: {} "
                        "| previous_height: {}",
                        width, height, previousWidth, previousHeight);
}

} // namespace Engine
