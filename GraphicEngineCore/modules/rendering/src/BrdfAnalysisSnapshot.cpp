#include "rendering/include/renderer/brdf/BrdfAnalysisSnapshot.h"

#include "rendering/include/renderer/brdf/Brdf.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string>

namespace {

auto IsSupportedModel(Engine::Renderer::BrdfModel model) noexcept -> bool {
  switch (model) {
  case Engine::Renderer::BrdfModel::Legacy:
  case Engine::Renderer::BrdfModel::GGX:
  case Engine::Renderer::BrdfModel::GTR:
  case Engine::Renderer::BrdfModel::Beckmann:
  case Engine::Renderer::BrdfModel::Gauss:
  case Engine::Renderer::BrdfModel::BlinnPhong:
  case Engine::Renderer::BrdfModel::AshikhminShirley:
  case Engine::Renderer::BrdfModel::Schlick:
  case Engine::Renderer::BrdfModel::Sawicki:
    return true;
  }
  return false;
}

auto ModelIdentifier(Engine::Renderer::BrdfModel model) -> std::string {
  switch (model) {
  case Engine::Renderer::BrdfModel::Legacy:
    return "legacy";
  case Engine::Renderer::BrdfModel::GGX:
    return "ggx";
  case Engine::Renderer::BrdfModel::GTR:
    return "gtr";
  case Engine::Renderer::BrdfModel::Beckmann:
    return "beckmann";
  case Engine::Renderer::BrdfModel::Gauss:
    return "gauss";
  case Engine::Renderer::BrdfModel::BlinnPhong:
    return "blinnPhong";
  case Engine::Renderer::BrdfModel::AshikhminShirley:
    return "ashikhminShirley";
  case Engine::Renderer::BrdfModel::Schlick:
    return "schlick";
  case Engine::Renderer::BrdfModel::Sawicki:
    return "sawicki";
  }
  return {};
}

auto ApplyMaterialVariant(Engine::SceneDocument& scene, uint64_t entityId,
                          const std::optional<size_t>& materialOverrideIndex,
                          const Engine::SceneMaterialDocument& sourceMaterial, Engine::Renderer::BrdfModel model,
                          const Engine::Renderer::BrdfAnalysis::BrdfAnalysisOverrides& overrides) -> Engine::Status {
  const auto entity = std::find_if(scene.entities.begin(), scene.entities.end(),
                                   [entityId](const auto& item) { return item.id == entityId; });
  if (entity == scene.entities.end()) {
    return std::unexpected(Engine::MakeError("BRDF analysis target entity was not found in the captured scene"));
  }

  Engine::SceneMaterialDocument material = sourceMaterial;
  material.brdfModel = ModelIdentifier(model);
  if (overrides.Roughness.has_value()) {
    material.roughness = *overrides.Roughness;
  }
  if (overrides.GtrGamma.has_value()) {
    material.gtrGamma = *overrides.GtrGamma;
  }
  if (materialOverrideIndex.has_value()) {
    if (*materialOverrideIndex > static_cast<size_t>((std::numeric_limits<int32_t>::max)()) ||
        *materialOverrideIndex >= entity->materialOverrides.max_size()) {
      return std::unexpected(Engine::MakeError("BRDF analysis material override index is out of range"));
    }
    if (entity->materialOverrides.size() <= *materialOverrideIndex) {
      entity->materialOverrides.resize(*materialOverrideIndex + 1);
    }
    entity->materialOverrides[*materialOverrideIndex] = std::move(material);
  } else {
    entity->materialGuid.reset();
    entity->material = std::move(material);
  }
  return {};
}

auto ValidateOutput(const Engine::RenderOutput& output, uint32_t width, uint32_t height) -> Engine::Status {
  if (output.Width != width || output.Height != height) {
    return std::unexpected(Engine::MakeError("BRDF analysis render output dimensions do not match the request"));
  }
  const auto planes = output.GetPlanes();
  if (planes.size() != 5) {
    return std::unexpected(Engine::MakeError("BRDF analysis render output must contain all five AOV planes"));
  }
  if (static_cast<size_t>(width) > (std::numeric_limits<size_t>::max)() / static_cast<size_t>(height)) {
    return std::unexpected(Engine::MakeError("BRDF analysis render dimensions exceed addressable storage"));
  }
  const size_t pixelCount = static_cast<size_t>(width) * height;
  for (size_t index = 0; index < planes.size(); ++index) {
    const Engine::RenderAov aov = static_cast<Engine::RenderAov>(index);
    const Engine::RenderAovDescriptor& descriptor = Engine::RenderOutput::GetDescriptor(aov);
    const Engine::RenderOutputPlane& plane = planes[index];
    if (plane.Aov != descriptor.Aov || plane.ChannelCount != descriptor.ChannelCount) {
      return std::unexpected(Engine::MakeError("BRDF analysis render output AOV planes are invalid or out of order"));
    }
    if (pixelCount > (std::numeric_limits<size_t>::max)() / descriptor.ChannelCount ||
        plane.Pixels.size() != pixelCount * descriptor.ChannelCount) {
      return std::unexpected(Engine::MakeError("BRDF analysis render output AOV sample count is invalid"));
    }
    if (!std::ranges::all_of(plane.Pixels, [](float sample) { return std::isfinite(sample); })) {
      return std::unexpected(Engine::MakeError("BRDF analysis render output contains a non-finite AOV sample"));
    }
  }
  return {};
}

auto ValidateInputs(const Engine::Renderer::BrdfAnalysis::BrdfAnalysisSnapshot& snapshot,
                    std::span<const Engine::Renderer::BrdfModel> models) -> Engine::Status {
  const uint32_t width = snapshot.FrameOverrides.OutputResolution.has_value()
                             ? snapshot.FrameOverrides.OutputResolution->Width
                             : snapshot.Width;
  const uint32_t height = snapshot.FrameOverrides.OutputResolution.has_value()
                              ? snapshot.FrameOverrides.OutputResolution->Height
                              : snapshot.Height;
  if (width == 0 || height == 0) {
    return std::unexpected(Engine::MakeError("BRDF analysis render dimensions must be positive"));
  }
  if (snapshot.FrameOverrides.Roughness.has_value() &&
      (!std::isfinite(*snapshot.FrameOverrides.Roughness) || *snapshot.FrameOverrides.Roughness < 0.0F ||
       *snapshot.FrameOverrides.Roughness > 1.0F)) {
    return std::unexpected(Engine::MakeError("BRDF analysis roughness override must be finite and in [0, 1]"));
  }
  if (snapshot.FrameOverrides.GtrGamma.has_value() &&
      (!std::isfinite(*snapshot.FrameOverrides.GtrGamma) ||
       *snapshot.FrameOverrides.GtrGamma < Engine::Renderer::Brdf::MinGamma ||
       *snapshot.FrameOverrides.GtrGamma > Engine::Renderer::Brdf::MaxGamma)) {
    return std::unexpected(Engine::MakeError("BRDF analysis GTR gamma override is outside its supported range"));
  }
  if (models.empty()) {
    return std::unexpected(Engine::MakeError("BRDF analysis requires at least one model"));
  }
  for (size_t index = 0; index < models.size(); ++index) {
    if (!IsSupportedModel(models[index])) {
      return std::unexpected(Engine::MakeError("BRDF analysis model is not supported"));
    }
    if (std::find(models.begin(), models.begin() + static_cast<std::ptrdiff_t>(index), models[index]) !=
        models.begin() + static_cast<std::ptrdiff_t>(index)) {
      return std::unexpected(Engine::MakeError("BRDF analysis models must be unique"));
    }
  }
  return {};
}

auto CreateRequest(const Engine::Renderer::BrdfAnalysis::BrdfAnalysisSnapshot& snapshot,
                   Engine::Renderer::BrdfModel model)
    -> Engine::Result<Engine::Renderer::BrdfAnalysis::BrdfAnalysisRenderRequest> {
  using namespace Engine::Renderer::BrdfAnalysis;
  BrdfAnalysisRenderRequest request;
  request.Scene = snapshot.Scene;
  request.Model = model;
  request.Material = snapshot.SourceMaterialProperties;
  request.Material.Model = model;
  request.Camera = snapshot.Camera;
  request.Width = snapshot.FrameOverrides.OutputResolution.has_value() ? snapshot.FrameOverrides.OutputResolution->Width
                                                                       : snapshot.Width;
  request.Height = snapshot.FrameOverrides.OutputResolution.has_value()
                       ? snapshot.FrameOverrides.OutputResolution->Height
                       : snapshot.Height;
  request.ShadowMapResolution = snapshot.ShadowMapResolution;
  request.MaxShadowCastersPerScene = snapshot.MaxShadowCastersPerScene;
  request.PostProcess = snapshot.PostProcess;
  request.FrameOverrides = snapshot.FrameOverrides;
  if (snapshot.FrameOverrides.Roughness.has_value()) {
    request.Material.Roughness = *snapshot.FrameOverrides.Roughness;
  }
  if (snapshot.FrameOverrides.GtrGamma.has_value()) {
    request.Material.GtrGamma = *snapshot.FrameOverrides.GtrGamma;
  }
  const Engine::Status materialStatus =
      ApplyMaterialVariant(request.Scene, snapshot.TargetEntityId, snapshot.MaterialOverrideIndex,
                           snapshot.SourceMaterial, model, snapshot.FrameOverrides);
  if (!materialStatus) {
    return std::unexpected(materialStatus.error());
  }
  return request;
}

} // namespace

auto Engine::Renderer::BrdfAnalysis::ExecuteBrdfAnalysisVariants(const BrdfAnalysisSnapshot& snapshot,
                                                                 std::span<const BrdfModel> models,
                                                                 const BrdfAnalysisRenderCallback& render)
    -> Engine::Result<std::vector<BrdfAnalysisVariant>> {
  auto result = CaptureBrdfAnalysisVariants(snapshot, models, render);
  if (!result) {
    return std::unexpected(result.error());
  }
  if (result->Status == BrdfAnalysisCaptureResult::State::Cancelled) {
    return std::unexpected(Engine::MakeError("BRDF analysis capture was cancelled"));
  }
  return std::move(result->Variants);
}

auto Engine::Renderer::BrdfAnalysis::CaptureBrdfAnalysisVariants(const BrdfAnalysisSnapshot& snapshot,
                                                                 std::span<const BrdfModel> models,
                                                                 const BrdfAnalysisRenderCallback& render,
                                                                 const BrdfAnalysisCaptureOptions& options)
    -> Engine::Result<BrdfAnalysisCaptureResult> {
  if (!render) {
    return std::unexpected(Engine::MakeError("BRDF analysis render callback is not set"));
  }
  auto sessionResult = BrdfAnalysisCaptureSession::Create(snapshot, models);
  if (!sessionResult) {
    return std::unexpected(sessionResult.error());
  }
  BrdfAnalysisCaptureSession session = std::move(*sessionResult);
  while (session.GetStatus() == BrdfAnalysisCaptureSessionState::Ready) {
    try {
      if (options.IsCancelled && options.IsCancelled()) {
        session.Cancel();
        break;
      }
      const BrdfAnalysisRenderRequest* request = nullptr;
      auto requestResult = session.GetNextRequest();
      if (!requestResult) {
        return std::unexpected(requestResult.error());
      }
      request = *requestResult;
      if (request == nullptr) {
        break;
      }
      Engine::Result<Engine::RenderOutput> output = render(*request);
      if (!output) {
        return std::unexpected(output.error());
      }
      if (options.IsCancelled && options.IsCancelled()) {
        session.Cancel();
        break;
      }
      const BrdfModel model = request->Model;
      const Engine::Status completion = session.CompleteCurrent(std::move(*output));
      if (!completion) {
        return std::unexpected(completion.error());
      }
      if (options.Progress) {
        options.Progress(session.GetCompletedCount(), session.GetTotalCount(), model);
      }
    } catch (const std::exception& exception) {
      return std::unexpected(
          Engine::MakeError(std::string("BRDF analysis capture callback failed: ") + exception.what()));
    } catch (...) {
      return std::unexpected(Engine::MakeError("BRDF analysis capture callback failed with an unknown exception"));
    }
  }
  return session.TakeResult();
}

Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession::BrdfAnalysisCaptureSession(BrdfAnalysisSnapshot snapshot,
                                                                                       std::vector<BrdfModel> models)
    : m_Snapshot(std::move(snapshot)), m_Models(std::move(models)) {
  m_Variants.reserve(m_Models.size());
}

auto Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession::Create(const BrdfAnalysisSnapshot& snapshot,
                                                                        std::span<const BrdfModel> models)
    -> Engine::Result<BrdfAnalysisCaptureSession> {
  const Engine::Status validation = ValidateInputs(snapshot, models);
  if (!validation) {
    return std::unexpected(validation.error());
  }
  auto firstRequest = CreateRequest(snapshot, models.front());
  if (!firstRequest) {
    return std::unexpected(firstRequest.error());
  }
  BrdfAnalysisCaptureSession session(snapshot, std::vector<BrdfModel>(models.begin(), models.end()));
  session.m_CurrentRequest = std::move(*firstRequest);
  return session;
}

auto Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession::GetNextRequest()
    -> Engine::Result<const BrdfAnalysisRenderRequest*> {
  if (m_Status == BrdfAnalysisCaptureSessionState::Cancelled ||
      m_Status == BrdfAnalysisCaptureSessionState::Completed) {
    return nullptr;
  }
  if (m_Status == BrdfAnalysisCaptureSessionState::Failed) {
    return std::unexpected(Engine::MakeError("BRDF analysis capture session has failed"));
  }
  if (m_CurrentRequest.has_value()) {
    return &*m_CurrentRequest;
  }
  if (m_Variants.size() >= m_Models.size()) {
    m_Status = BrdfAnalysisCaptureSessionState::Completed;
    return nullptr;
  }
  auto request = CreateRequest(m_Snapshot, m_Models[m_Variants.size()]);
  if (!request) {
    Fail();
    return std::unexpected(request.error());
  }
  m_CurrentRequest = std::move(*request);
  return &*m_CurrentRequest;
}

auto Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession::CompleteCurrent(Engine::RenderOutput output)
    -> Engine::Status {
  if (m_Status != BrdfAnalysisCaptureSessionState::Ready || !m_CurrentRequest.has_value()) {
    return std::unexpected(Engine::MakeError("BRDF analysis capture session has no active render request"));
  }
  const Engine::Status validation = ValidateOutput(output, m_CurrentRequest->Width, m_CurrentRequest->Height);
  if (!validation) {
    Fail();
    return std::unexpected(validation.error());
  }
  m_Variants.push_back(BrdfAnalysisVariant{.Request = std::move(*m_CurrentRequest), .Output = std::move(output)});
  m_CurrentRequest.reset();
  if (m_Variants.size() == m_Models.size()) {
    m_Status = BrdfAnalysisCaptureSessionState::Completed;
  }
  return {};
}

void Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession::Cancel() noexcept {
  if (m_Status == BrdfAnalysisCaptureSessionState::Ready) {
    m_Variants.clear();
    m_CurrentRequest.reset();
    m_Status = BrdfAnalysisCaptureSessionState::Cancelled;
  }
}

void Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession::Fail() noexcept {
  m_Variants.clear();
  m_CurrentRequest.reset();
  m_Status = BrdfAnalysisCaptureSessionState::Failed;
}

auto Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession::TakeResult()
    -> Engine::Result<BrdfAnalysisCaptureResult> {
  if (m_ResultTaken) {
    return std::unexpected(Engine::MakeError("BRDF analysis capture result was already taken"));
  }
  if (m_Status == BrdfAnalysisCaptureSessionState::Ready) {
    return std::unexpected(Engine::MakeError("BRDF analysis capture is still in progress"));
  }
  if (m_Status == BrdfAnalysisCaptureSessionState::Failed) {
    return std::unexpected(Engine::MakeError("BRDF analysis capture session has failed"));
  }
  m_ResultTaken = true;
  BrdfAnalysisCaptureResult result;
  if (m_Status == BrdfAnalysisCaptureSessionState::Cancelled) {
    result.Status = BrdfAnalysisCaptureResult::State::Cancelled;
  } else {
    result.Variants = std::move(m_Variants);
  }
  return result;
}
