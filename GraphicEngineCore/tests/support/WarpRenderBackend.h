#pragma once

#include "rendering/include/RenderBackend.h"
#include "runtime/include/Window.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <d3d11.h>
#include <nvrhi/d3d11.h>
#include <wrl/client.h>

namespace EngineTests {
class WarpDevice {
public:
  bool Initialize() {
    D3D_FEATURE_LEVEL featureLevel{};
    const HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                             &m_Device, &featureLevel, &m_Context);
    (void)featureLevel;
    if (FAILED(result) || !m_Context) {
      return false;
    }

    nvrhi::d3d11::DeviceDesc description;
    description.context = m_Context.Get();
    m_NvrhiDevice = nvrhi::d3d11::createDevice(description);
    return m_NvrhiDevice != nullptr;
  }

  nvrhi::IDevice* GetDevice() const { return m_NvrhiDevice.Get(); }

private:
  Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_Context;
  nvrhi::DeviceHandle m_NvrhiDevice;
};

class WarpRenderBackend final : public Engine::IRenderBackend {
public:
  explicit WarpRenderBackend(nvrhi::IDevice* device) : m_Device(device) {}

  bool Initialize(Engine::IWindow*) override { return m_Device != nullptr; }
  void Present() override {}
  void Resize(int, int) override {}
  void* GetDevice() const override { return m_Device; }
  void* GetDefaultFramebuffer() const override { return nullptr; }

private:
  nvrhi::IDevice* m_Device;
};
} // namespace EngineTests
