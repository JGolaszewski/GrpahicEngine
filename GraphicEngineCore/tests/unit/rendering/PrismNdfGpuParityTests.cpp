#include "rendering/include/RenderBackend.h"
#include "rendering/include/RenderSystem.h"
#include "rendering/include/renderer/Material.h"
#include "runtime/include/PrismNdfData.h"
#include "scene/include/Scene.h"
#include "scene/include/components/EcsComponents.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nvrhi/d3d11.h>

namespace {
using Microsoft::WRL::ComPtr;

struct GpuCase {
  std::array<float, 48> coefficients{};
  float x = 0.0F;
  float y = 0.0F;
  float residual = 0.0F;
  float normalization = 1.0F;
};
static_assert(sizeof(GpuCase) == 208);

auto Factorial(uint32_t value) -> double {
  double result = 1.0;
  for (uint32_t factor = 2; factor <= value; ++factor) {
    result *= factor;
  }
  return result;
}

auto IndependentBasis(uint32_t n, int32_t m, double x, double y) -> double {
  const uint32_t absoluteM = static_cast<uint32_t>(std::abs(m));
  const double radiusSquared = x * x + y * y;
  if (radiusSquared > 1.0) {
    return 0.0;
  }
  const double radius = std::sqrt(radiusSquared);
  double radial = 0.0;
  for (uint32_t k = 0; k <= (n - absoluteM) / 2; ++k) {
    const double sign = (k % 2U == 0U) ? 1.0 : -1.0;
    radial += sign * Factorial(n - k) /
              (Factorial(k) * Factorial((n + absoluteM) / 2U - k) * Factorial((n - absoluteM) / 2U - k)) *
              std::pow(radius, static_cast<int>(n - 2U * k));
  }
  const double angle = std::atan2(y, x);
  return radial * (m < 0 ? std::sin(absoluteM * angle) : std::cos(absoluteM * angle));
}

auto Reference(const GpuCase& testCase) -> double {
  uint32_t index = 0;
  double base = 0.0;
  for (uint32_t n = 0; n <= Engine::PrismNdfDegree; ++n) {
    for (int32_t m = -static_cast<int32_t>(n); m <= static_cast<int32_t>(n); m += 2, ++index) {
      base += static_cast<double>(testCase.coefficients[index]) * IndependentBasis(n, m, testCase.x, testCase.y);
    }
  }
  return std::max(base + static_cast<double>(testCase.residual), 0.0) *
         (testCase.x * testCase.x + testCase.y * testCase.y <= 1.0 ? 1.0 : 0.0) *
         static_cast<double>(testCase.normalization);
}

auto Utf8(std::wstring_view value) -> std::string {
  const int required =
      WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (required <= 0) {
    return {};
  }
  std::string converted(static_cast<size_t>(required), '\0');
  (void)WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), converted.data(), required,
                            nullptr, nullptr);
  return converted;
}

auto CreateNvidiaDevice(ComPtr<IDXGIAdapter1>& selectedAdapter, ComPtr<ID3D11Device>& device,
                        ComPtr<ID3D11DeviceContext>& context) -> bool {
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return false;
  }
  for (UINT adapterIndex = 0; factory->EnumAdapters1(adapterIndex, &selectedAdapter) != DXGI_ERROR_NOT_FOUND;
       ++adapterIndex) {
    DXGI_ADAPTER_DESC1 description{};
    if (FAILED(selectedAdapter->GetDesc1(&description))) {
      selectedAdapter.Reset();
      continue;
    }
    if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0U && description.VendorId == 0x10DEU) {
      const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
      D3D_FEATURE_LEVEL selectedLevel = D3D_FEATURE_LEVEL_11_0;
      const HRESULT result = D3D11CreateDevice(
          selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
          static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device, &selectedLevel, &context);
      if (SUCCEEDED(result)) {
        return true;
      }
      if (result == E_INVALIDARG) {
        const D3D_FEATURE_LEVEL fallbackLevel = D3D_FEATURE_LEVEL_11_0;
        const HRESULT fallbackResult =
            D3D11CreateDevice(selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                              &fallbackLevel, 1, D3D11_SDK_VERSION, &device, &selectedLevel, &context);
        if (SUCCEEDED(fallbackResult)) {
          return true;
        }
      }
    }
    selectedAdapter.Reset();
  }
  return false;
}

auto MakeStructuredBuffer(ID3D11Device* device, const void* initialData, uint32_t stride, uint32_t count,
                          UINT bindFlags, ID3D11Buffer** buffer) -> HRESULT {
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = stride * count;
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = bindFlags;
  description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  description.StructureByteStride = stride;
  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = initialData;
  return device->CreateBuffer(&description, initialData != nullptr ? &data : nullptr, buffer);
}

class NvidiaRenderBackend final : public Engine::IRenderBackend {
public:
  explicit NvidiaRenderBackend(ID3D11DeviceContext* context) {
    nvrhi::d3d11::DeviceDesc description;
    description.context = context;
    m_Device = nvrhi::d3d11::createDevice(description);
  }

  bool Initialize(Engine::IWindow*) override { return m_Device != nullptr; }
  void Present() override {}
  void Resize(int, int) override {}
  void* GetDevice() const override { return m_Device.Get(); }
  void* GetDefaultFramebuffer() const override { return nullptr; }

private:
  nvrhi::DeviceHandle m_Device;
};

auto TimestampRender(Engine::RenderSystem& renderer, ID3D11DeviceContext* context, Engine::Scene& scene,
                     const Engine::Renderer::CameraView& camera, uint32_t width, uint32_t height) -> double {
  D3D11_QUERY_DESC disjointDescription{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
  D3D11_QUERY_DESC timestampDescription{D3D11_QUERY_TIMESTAMP, 0};
  ComPtr<ID3D11Query> disjoint;
  ComPtr<ID3D11Query> start;
  ComPtr<ID3D11Query> end;
  ID3D11Device* rawDevice = nullptr;
  context->GetDevice(&rawDevice);
  ComPtr<ID3D11Device> device;
  device.Attach(rawDevice);
  EXPECT_NE(device, nullptr);
  if (device == nullptr) {
    return -1.0;
  }
  EXPECT_HRESULT_SUCCEEDED(device->CreateQuery(&disjointDescription, &disjoint));
  EXPECT_HRESULT_SUCCEEDED(device->CreateQuery(&timestampDescription, &start));
  EXPECT_HRESULT_SUCCEEDED(device->CreateQuery(&timestampDescription, &end));
  if (disjoint == nullptr || start == nullptr || end == nullptr) {
    return -1.0;
  }

  context->Begin(disjoint.Get());
  context->End(start.Get());
  const auto ticket =
      renderer.BeginFinalRender(scene, camera, width, height, {}, {Engine::EnvironmentPreset::Studio, 1.0F});
  EXPECT_TRUE(ticket.has_value()) << (ticket.has_value() ? "" : ticket.error().Message);
  if (!ticket.has_value()) {
    context->End(disjoint.Get());
    return -1.0;
  }
  context->End(end.Get());
  context->End(disjoint.Get());
  context->Flush();

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  std::optional<Engine::RenderOutput> output;
  while (!output.has_value() && std::chrono::steady_clock::now() < deadline) {
    auto polled = renderer.PollFinalRender(*ticket);
    EXPECT_TRUE(polled.has_value()) << (polled.has_value() ? "" : polled.error().Message);
    if (!polled.has_value()) {
      return -1.0;
    }
    if (polled->has_value()) {
      output = std::move(**polled);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  EXPECT_TRUE(output.has_value()) << "GPU final render did not complete before the timestamp query timeout";
  if (!output.has_value()) {
    const auto cancelled = renderer.CancelFinalRender(*ticket);
    EXPECT_TRUE(cancelled.has_value()) << (cancelled.has_value() ? "" : cancelled.error().Message);
    return -1.0;
  }

  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
  UINT64 startData = 0;
  UINT64 endData = 0;
  const auto waitForQuery = [context, &deadline](ID3D11Query* query, void* data, UINT dataSize) {
    const auto queryDeadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(30));
    HRESULT result = S_FALSE;
    while (result == S_FALSE && std::chrono::steady_clock::now() < queryDeadline) {
      result = context->GetData(query, data, dataSize, 0);
      if (result == S_FALSE) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    return result;
  };
  EXPECT_HRESULT_SUCCEEDED(waitForQuery(disjoint.Get(), &disjointData, sizeof(disjointData)));
  EXPECT_HRESULT_SUCCEEDED(waitForQuery(start.Get(), &startData, sizeof(startData)));
  EXPECT_HRESULT_SUCCEEDED(waitForQuery(end.Get(), &endData, sizeof(endData)));
  EXPECT_FALSE(disjointData.Disjoint);
  if (disjointData.Disjoint || disjointData.Frequency == 0U || endData < startData) {
    return -1.0;
  }
  const auto pixels = output->GetPixels(Engine::RenderAov::SpecularLighting);
  EXPECT_FALSE(pixels.empty());
  return 1000.0 * static_cast<double>(endData - startData) / static_cast<double>(disjointData.Frequency);
}
} // namespace

TEST(PrismNdfGpuParityTests, NvidiaD3D11MatchesIndependentDoubleReferenceForAllTermsAndEdgeCases) {
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (!CreateNvidiaDevice(adapter, device, context)) {
    GTEST_SKIP() << "No usable NVIDIA D3D11 adapter is available; this hardware validation requires a discrete GPU";
  }
  DXGI_ADAPTER_DESC1 adapterDescription{};
  ASSERT_HRESULT_SUCCEEDED(adapter->GetDesc1(&adapterDescription));
  ASSERT_EQ(adapterDescription.VendorId, 0x10DEU);
  SCOPED_TRACE("NVIDIA D3D11 hardware adapter selected");

  std::ifstream shaderFile(ENGINE_PRISM_NDF_GPU_PARITY_SHADER, std::ios::binary);
  ASSERT_TRUE(shaderFile) << "Compute shader was not compiled by the build: " << ENGINE_PRISM_NDF_GPU_PARITY_SHADER;
  const std::vector<char> shaderBytes(std::istreambuf_iterator<char>(shaderFile), {});
  ASSERT_FALSE(shaderBytes.empty());
  ComPtr<ID3D11ComputeShader> shader;
  ASSERT_HRESULT_SUCCEEDED(device->CreateComputeShader(shaderBytes.data(), shaderBytes.size(), nullptr, &shader));

  constexpr std::array points = {std::array{0.237, -0.413}, std::array{0.6, 0.7999999}, std::array{1.0, 0.0},
                                 std::array{0.0, -1.0}, std::array{1.01, 0.15}};
  std::vector<GpuCase> cases;
  for (uint32_t coefficient = 0; coefficient < Engine::PrismNdfCoefficientCount; ++coefficient) {
    for (const auto& point : points) {
      GpuCase testCase;
      testCase.coefficients[0] = 3.0F;
      testCase.coefficients[coefficient] += 0.35F;
      testCase.x = static_cast<float>(point[0]);
      testCase.y = static_cast<float>(point[1]);
      testCase.normalization = 1.37F;
      cases.push_back(testCase);
    }
  }
  GpuCase signedResidual;
  signedResidual.coefficients[0] = 1.0F;
  signedResidual.residual = -0.25F;
  signedResidual.normalization = 1.7F;
  cases.push_back(signedResidual);
  GpuCase residualClamp = signedResidual;
  residualClamp.residual = -2.0F;
  cases.push_back(residualClamp);
  GpuCase exteriorResidual = signedResidual;
  exteriorResidual.x = 1.01F;
  exteriorResidual.residual = 5.0F;
  cases.push_back(exteriorResidual);
  GpuCase allTerms;
  allTerms.x = 0.31F;
  allTerms.y = -0.47F;
  allTerms.normalization = 0.618F;
  for (uint32_t index = 0; index < Engine::PrismNdfCoefficientCount; ++index) {
    allTerms.coefficients[index] =
        (index % 2U == 0U ? 1.0F : -1.0F) * (0.015F + 0.001F * static_cast<float>(index));
  }
  cases.push_back(allTerms);

  ComPtr<ID3D11Buffer> inputBuffer;
  ASSERT_HRESULT_SUCCEEDED(MakeStructuredBuffer(device.Get(), cases.data(), sizeof(GpuCase),
                                                static_cast<uint32_t>(cases.size()), D3D11_BIND_SHADER_RESOURCE,
                                                &inputBuffer));
  D3D11_SHADER_RESOURCE_VIEW_DESC inputViewDescription{};
  inputViewDescription.Format = DXGI_FORMAT_UNKNOWN;
  inputViewDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
  inputViewDescription.Buffer.NumElements = static_cast<UINT>(cases.size());
  ComPtr<ID3D11ShaderResourceView> inputView;
  ASSERT_HRESULT_SUCCEEDED(device->CreateShaderResourceView(inputBuffer.Get(), &inputViewDescription, &inputView));

  ComPtr<ID3D11Buffer> outputBuffer;
  ASSERT_HRESULT_SUCCEEDED(MakeStructuredBuffer(device.Get(), nullptr, sizeof(float),
                                                static_cast<uint32_t>(cases.size()), D3D11_BIND_UNORDERED_ACCESS,
                                                &outputBuffer));
  D3D11_UNORDERED_ACCESS_VIEW_DESC outputViewDescription{};
  outputViewDescription.Format = DXGI_FORMAT_UNKNOWN;
  outputViewDescription.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  outputViewDescription.Buffer.NumElements = static_cast<UINT>(cases.size());
  ComPtr<ID3D11UnorderedAccessView> outputView;
  ASSERT_HRESULT_SUCCEEDED(device->CreateUnorderedAccessView(outputBuffer.Get(), &outputViewDescription, &outputView));

  ID3D11ShaderResourceView* inputViews[] = {inputView.Get()};
  ID3D11UnorderedAccessView* outputViews[] = {outputView.Get()};
  context->CSSetShader(shader.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, inputViews);
  context->CSSetUnorderedAccessViews(0, 1, outputViews, nullptr);
  context->Dispatch((static_cast<UINT>(cases.size()) + 63U) / 64U, 1, 1);
  ID3D11ShaderResourceView* nullInput[] = {nullptr};
  ID3D11UnorderedAccessView* nullOutput[] = {nullptr};
  context->CSSetShaderResources(0, 1, nullInput);
  context->CSSetUnorderedAccessViews(0, 1, nullOutput, nullptr);

  D3D11_BUFFER_DESC readbackDescription{};
  readbackDescription.ByteWidth = static_cast<UINT>(cases.size() * sizeof(float));
  readbackDescription.Usage = D3D11_USAGE_STAGING;
  readbackDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> readback;
  ASSERT_HRESULT_SUCCEEDED(device->CreateBuffer(&readbackDescription, nullptr, &readback));
  context->CopyResource(readback.Get(), outputBuffer.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  ASSERT_HRESULT_SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
  const auto* results = static_cast<const float*>(mapped.pData);
  for (size_t index = 0; index < cases.size(); ++index) {
    const double expected = Reference(cases[index]);
    EXPECT_NEAR(static_cast<double>(results[index]), expected, 1.0e-4 + 5.0e-4 * std::abs(expected))
        << "case=" << index << " x=" << cases[index].x << " y=" << cases[index].y;
  }
  context->Unmap(readback.Get(), 0);
}

TEST(PrismNdfGpuParityTests, NvidiaD3D11TimestampMeasuresPrismEnvironmentRenderCost) {
  std::array<char, 2> enableTiming{};
  if (GetEnvironmentVariableA("ENGINE_PRISM_GPU_TIMING", enableTiming.data(),
                              static_cast<DWORD>(enableTiming.size())) != 1U ||
      enableTiming[0] != '1') {
    GTEST_SKIP() << "Set ENGINE_PRISM_GPU_TIMING=1 to run the opt-in hardware timestamp workload";
  }

  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (!CreateNvidiaDevice(adapter, device, context)) {
    GTEST_SKIP() << "No usable NVIDIA D3D11 adapter is available; WARP timing is not substituted";
  }
  DXGI_ADAPTER_DESC1 adapterDescription{};
  ASSERT_HRESULT_SUCCEEDED(adapter->GetDesc1(&adapterDescription));
  ASSERT_EQ(adapterDescription.VendorId, 0x10DEU);

  NvidiaRenderBackend backend(context.Get());
  Engine::RenderSystem renderer(&backend);
  ASSERT_TRUE(renderer.IsValid());
  auto payload = std::make_shared<Engine::PrismNdfData>();
  payload->zernikeCoefficients[0] = 1.0F;
  payload->normalization = 1.0F / std::numbers::pi_v<float>;
  Engine::Renderer::MaterialProperties properties;
  properties.Model = Engine::Renderer::BrdfModel::PrismNdf;
  properties.Roughness = 0.4F;
  properties.GeneratedNdf = payload;
  auto material = renderer.CreateMaterial(properties);
  ASSERT_NE(material, nullptr);
  ASSERT_TRUE(renderer.CreatePrimitive(Engine::PrimitiveShape::Sphere, material));
  Engine::Scene scene;
  const auto sequence = scene.QueueCreate("PRISM GPU timestamp workload");
  const auto created = scene.sync();
  ASSERT_EQ(created.size(), 1U);
  ASSERT_EQ(created.front().Sequence, sequence);
  ASSERT_TRUE(created.front().Outcome);
  (void)scene.QueueAdd(created.front().Entity, Engine::Ecs::MeshRenderer{.PrimitiveType = "sphere"});
  ASSERT_TRUE(scene.Update(0.0F));
  Engine::Renderer::CameraView camera;
  camera.Position = {0.0F, 0.0F, -3.0F};

  struct Workload {
    uint32_t Width;
    uint32_t Height;
    uint32_t Repetitions;
  };
  constexpr std::array workloads = {Workload{512, 512, 7}, Workload{1024, 1024, 5}, Workload{1920, 1080, 3}};
  const std::string adapterName = Utf8(adapterDescription.Description);
  std::cout << "PRISM_GPU_TIMESTAMP adapter=\"" << adapterName
            << "\" adapter_vendor=0x10DE workload=PRISM sphere, generated NDF, Studio environment, "
               "128 shader samples per covered pixel\n";
  for (const Workload& workload : workloads) {
    auto warmup = renderer.RenderFinal(scene, camera, workload.Width, workload.Height, {},
                                       {Engine::EnvironmentPreset::Studio, 1.0F});
    ASSERT_TRUE(warmup.has_value()) << (warmup.has_value() ? "" : warmup.error().Message);
    std::vector<double> milliseconds;
    milliseconds.reserve(workload.Repetitions);
    for (uint32_t repetition = 0; repetition < workload.Repetitions; ++repetition) {
      const double elapsed = TimestampRender(renderer, context.Get(), scene, camera, workload.Width, workload.Height);
      ASSERT_GT(elapsed, 0.0);
      milliseconds.push_back(elapsed);
    }
    std::ranges::sort(milliseconds);
    const double median = milliseconds[milliseconds.size() / 2U];
    std::cout << "PRISM_GPU_TIMESTAMP resolution=" << workload.Width << 'x' << workload.Height
              << " covered_pixel_workload_samples=128 median_ms=" << median << " repetitions=" << workload.Repetitions
              << '\n';
    if (workload.Width == 1920U) {
      EXPECT_LT(median, 16.67) << "1080p PRISM workload exceeded the 60 FPS frame budget";
    }
  }
}
