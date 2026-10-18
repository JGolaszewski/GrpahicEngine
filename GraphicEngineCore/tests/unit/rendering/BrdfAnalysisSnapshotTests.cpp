#include "rendering/include/renderer/brdf/BrdfAnalysisSnapshot.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Engine::Renderer::BrdfAnalysis::BrdfAnalysisSnapshot;
using Engine::Renderer::BrdfAnalysis::BrdfAnalysisVariant;
using Engine::Renderer::BrdfAnalysis::CaptureBrdfAnalysisVariants;
using Engine::Renderer::BrdfAnalysis::ExecuteBrdfAnalysisVariants;

auto MakeOutput(uint32_t width, uint32_t height) -> Engine::Result<Engine::RenderOutput> {
  std::vector<Engine::RenderOutputPlane> planes;
  for (uint8_t index = 0; index < 5; ++index) {
    const auto aov = static_cast<Engine::RenderAov>(index);
    const uint32_t channels = Engine::RenderOutput::GetDescriptor(aov).ChannelCount;
    planes.push_back({.Aov = aov,
                      .ChannelCount = channels,
                      .Pixels = std::vector<float>(static_cast<size_t>(width) * height * channels, 0.25F)});
  }
  return Engine::RenderOutput::CreateFromPlanes(width, height, std::move(planes));
}

auto MakeSnapshot() -> BrdfAnalysisSnapshot {
  BrdfAnalysisSnapshot snapshot;
  snapshot.Scene.unitsPerMeter = 0.5F;
  Engine::SceneEntityDocument target;
  target.id = 42;
  target.name = "Selected material owner";
  target.materialGuid = "shared-material-guid";
  target.materialOverrides.resize(2);
  target.materialOverrides[0] = Engine::SceneMaterialDocument{.brdfModel = "ggx"};
  snapshot.Scene.entities.push_back(target);
  Engine::SceneEntityDocument other;
  other.id = 43;
  other.name = "Unselected object";
  other.material = Engine::SceneMaterialDocument{};
  other.material->roughness = 0.8F;
  snapshot.Scene.entities.push_back(other);
  Engine::SceneLightDocument light;
  light.id = 50;
  light.intensity = 7.0F;
  snapshot.Scene.lights.push_back(light);
  Engine::SceneCameraDocument camera;
  camera.id = 60;
  camera.outputWidth = 640;
  camera.outputHeight = 480;
  snapshot.Scene.cameras.push_back(camera);

  snapshot.TargetEntityId = 42;
  snapshot.MaterialOverrideIndex = 0;
  snapshot.SourceMaterial.brdfModel = "ggx";
  snapshot.SourceMaterial.roughness = 0.35F;
  snapshot.SourceMaterial.baseColorTextureGuid = "base-color-guid";
  snapshot.SourceMaterialProperties.Roughness = 0.35F;
  snapshot.SourceMaterialProperties.Model = Engine::Renderer::BrdfModel::GGX;
  snapshot.Camera.Position = {1.0F, 2.0F, 3.0F};
  snapshot.Camera.FieldOfViewDegrees = 52.0F;
  snapshot.Width = 320;
  snapshot.Height = 200;
  snapshot.ShadowMapResolution = 1024;
  snapshot.MaxShadowCastersPerScene = 3;
  return snapshot;
}

TEST(BrdfAnalysisSnapshotTests, VariantsOnlyChangeSelectedMaterialModelAndPreserveSnapshot) {
  const BrdfAnalysisSnapshot snapshot = MakeSnapshot();
  const std::array models{Engine::Renderer::BrdfModel::GGX, Engine::Renderer::BrdfModel::GTR};
  size_t callbackCount = 0;
  auto render = [&callbackCount](const auto& request) -> Engine::Result<Engine::RenderOutput> {
    ++callbackCount;
    EXPECT_EQ(request.Width, 320U);
    EXPECT_EQ(request.Height, 200U);
    EXPECT_EQ(request.ShadowMapResolution, 1024U);
    EXPECT_EQ(request.MaxShadowCastersPerScene, 3U);
    EXPECT_FLOAT_EQ(request.Camera.Position[1], 2.0F);
    EXPECT_FLOAT_EQ(request.Camera.FieldOfViewDegrees, 52.0F);
    EXPECT_EQ(request.Scene.unitsPerMeter, 0.5F);
    EXPECT_EQ(request.Scene.lights.size(), 1U);
    EXPECT_FLOAT_EQ(request.Scene.lights[0].intensity, 7.0F);
    EXPECT_EQ(request.Scene.cameras.size(), 1U);
    EXPECT_EQ(request.Scene.cameras[0].outputWidth, 640U);
    EXPECT_EQ(request.Scene.entities.size(), 2U);
    EXPECT_FLOAT_EQ(request.Scene.entities[1].material->roughness, 0.8F);
    EXPECT_EQ(request.Scene.entities[0].materialOverrides[0]->brdfModel,
              request.Model == Engine::Renderer::BrdfModel::GGX ? "ggx" : "gtr");
    EXPECT_EQ(request.Scene.entities[0].materialOverrides[0]->baseColorTextureGuid, "base-color-guid");
    EXPECT_EQ(request.Scene.entities[0].materialGuid, "shared-material-guid");
    EXPECT_EQ(request.Material.Model, request.Model);

    return MakeOutput(request.Width, request.Height);
  };

  const auto result = ExecuteBrdfAnalysisVariants(snapshot, models, render);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), models.size());
  EXPECT_EQ(callbackCount, models.size());
  EXPECT_EQ((*result)[0].Request.Model, models[0]);
  EXPECT_EQ((*result)[1].Request.Model, models[1]);
  EXPECT_EQ(snapshot.Scene.entities[0].materialOverrides[0].value_or(Engine::SceneMaterialDocument{}).brdfModel, "ggx");
  EXPECT_FLOAT_EQ(snapshot.Scene.entities[1].material.value_or(Engine::SceneMaterialDocument{}).roughness, 0.8F);
  EXPECT_EQ(snapshot.SourceMaterial.brdfModel, "ggx");
  EXPECT_EQ(snapshot.SourceMaterialProperties.Model, Engine::Renderer::BrdfModel::GGX);
}

TEST(BrdfAnalysisSnapshotTests, StopsWithoutReturningPartialResultsWhenVariantRenderFails) {
  const BrdfAnalysisSnapshot snapshot = MakeSnapshot();
  const std::array models{Engine::Renderer::BrdfModel::GGX, Engine::Renderer::BrdfModel::GTR};
  size_t callbackCount = 0;
  const auto render = [&callbackCount](const auto&) -> Engine::Result<Engine::RenderOutput> {
    ++callbackCount;
    if (callbackCount == 2) {
      return std::unexpected(Engine::MakeError("expected test failure"));
    }
    return MakeOutput(320, 200);
  };

  const auto result = ExecuteBrdfAnalysisVariants(snapshot, models, render);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().Message, "expected test failure");
  EXPECT_EQ(callbackCount, 2U);
  EXPECT_EQ(snapshot.Scene.entities[0].materialOverrides[0].value_or(Engine::SceneMaterialDocument{}).brdfModel, "ggx");
}

TEST(BrdfAnalysisSnapshotTests, RejectsDuplicateModelsBeforeRendering) {
  const BrdfAnalysisSnapshot snapshot = MakeSnapshot();
  const std::array models{Engine::Renderer::BrdfModel::GGX, Engine::Renderer::BrdfModel::GGX};
  size_t callbackCount = 0;
  const auto render = [&callbackCount](const auto&) -> Engine::Result<Engine::RenderOutput> {
    ++callbackCount;
    return MakeOutput(320, 200);
  };

  const auto result = ExecuteBrdfAnalysisVariants(snapshot, models, render);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(callbackCount, 0U);
}

TEST(BrdfAnalysisSnapshotTests, InlinesCapturedMaterialForSharedAssetTargetWithoutChangingSourceDocument) {
  BrdfAnalysisSnapshot snapshot = MakeSnapshot();
  snapshot.MaterialOverrideIndex.reset();
  snapshot.Scene.entities[0].materialGuid = "shared-material-guid";
  snapshot.Scene.entities[0].materialOverrides.clear();
  const std::array models{Engine::Renderer::BrdfModel::GTR};
  const auto render = [](const auto& request) -> Engine::Result<Engine::RenderOutput> {
    if (request.Scene.entities.size() != 2U || !request.Scene.entities[0].material.has_value()) {
      return std::unexpected(Engine::MakeError("variant material was not inlined"));
    }
    EXPECT_FALSE(request.Scene.entities[0].materialGuid.has_value());
    EXPECT_EQ(request.Scene.entities[0].material->brdfModel, "gtr");
    EXPECT_FLOAT_EQ(request.Scene.entities[0].material->roughness, 0.35F);
    return MakeOutput(request.Width, request.Height);
  };

  const auto result = ExecuteBrdfAnalysisVariants(snapshot, models, render);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1U);
  EXPECT_EQ(snapshot.Scene.entities[0].materialGuid, "shared-material-guid");
  EXPECT_FALSE(snapshot.Scene.entities[0].material.has_value());
  EXPECT_EQ(snapshot.SourceMaterial.brdfModel, "ggx");
  EXPECT_EQ(snapshot.SourceMaterial.baseColorTextureGuid, "base-color-guid");
  EXPECT_FLOAT_EQ(snapshot.SourceMaterial.roughness, 0.35F);
}

TEST(BrdfAnalysisSnapshotTests, CreatesVariantsForEverySupportedBrdfModel) {
  const BrdfAnalysisSnapshot snapshot = MakeSnapshot();
  const std::array models{
      std::pair{Engine::Renderer::BrdfModel::Legacy, std::string_view{"legacy"}},
      std::pair{Engine::Renderer::BrdfModel::GGX, std::string_view{"ggx"}},
      std::pair{Engine::Renderer::BrdfModel::GTR, std::string_view{"gtr"}},
      std::pair{Engine::Renderer::BrdfModel::Beckmann, std::string_view{"beckmann"}},
      std::pair{Engine::Renderer::BrdfModel::Gauss, std::string_view{"gauss"}},
      std::pair{Engine::Renderer::BrdfModel::BlinnPhong, std::string_view{"blinnPhong"}},
      std::pair{Engine::Renderer::BrdfModel::AshikhminShirley, std::string_view{"ashikhminShirley"}},
      std::pair{Engine::Renderer::BrdfModel::Schlick, std::string_view{"schlick"}},
      std::pair{Engine::Renderer::BrdfModel::Sawicki, std::string_view{"sawicki"}},
  };
  std::vector<Engine::Renderer::BrdfModel> requestedModels;
  requestedModels.reserve(models.size());
  for (const auto& [model, unusedIdentifier] : models) {
    (void)unusedIdentifier;
    requestedModels.push_back(model);
  }
  const auto render = [](const auto& request) -> Engine::Result<Engine::RenderOutput> {
    return MakeOutput(request.Width, request.Height);
  };

  const auto result = ExecuteBrdfAnalysisVariants(snapshot, requestedModels, render);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), models.size());
  for (size_t index = 0; index < models.size(); ++index) {
    EXPECT_EQ((*result)[index].Request.Model, models[index].first);
    EXPECT_EQ((*result)[index].Request.Material.Model, models[index].first);
    EXPECT_EQ((*result)[index]
                  .Request.Scene.entities[0]
                  .materialOverrides[0]
                  .value_or(Engine::SceneMaterialDocument{})
                  .brdfModel,
              models[index].second);
  }
  EXPECT_EQ(snapshot.Scene.entities[0].materialOverrides[0].value_or(Engine::SceneMaterialDocument{}).brdfModel, "ggx");
}

TEST(BrdfAnalysisSnapshotTests, AppliesSerializableFrameOverridesEquallyWithoutMutatingAuthoredData) {
  BrdfAnalysisSnapshot snapshot = MakeSnapshot();
  snapshot.FrameOverrides.OutputResolution =
      Engine::Renderer::BrdfAnalysis::BrdfAnalysisOverrides::Resolution{1280, 720};
  snapshot.FrameOverrides.Roughness = 0.72F;
  snapshot.FrameOverrides.GtrGamma = 3.25F;
  const std::array models{Engine::Renderer::BrdfModel::GGX, Engine::Renderer::BrdfModel::GTR};
  std::vector<Engine::Renderer::BrdfModel> rendered;
  const auto render = [&rendered](const auto& request) -> Engine::Result<Engine::RenderOutput> {
    rendered.push_back(request.Model);
    EXPECT_EQ(request.Width, 1280U);
    EXPECT_EQ(request.Height, 720U);
    EXPECT_FLOAT_EQ(request.Material.Roughness, 0.72F);
    EXPECT_FLOAT_EQ(request.Material.GtrGamma, 3.25F);
    const auto& material = request.Scene.entities[0].materialOverrides[0].value();
    EXPECT_FLOAT_EQ(material.roughness, 0.72F);
    EXPECT_FLOAT_EQ(material.gtrGamma, 3.25F);
    return MakeOutput(request.Width, request.Height);
  };
  const auto capture = CaptureBrdfAnalysisVariants(snapshot, models, render);
  ASSERT_TRUE(capture.has_value());
  EXPECT_EQ(capture->Status, Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureResult::State::Completed);
  ASSERT_EQ(capture->Variants.size(), 2U);
  EXPECT_EQ(rendered, std::vector<Engine::Renderer::BrdfModel>(models.begin(), models.end()));
  EXPECT_EQ(snapshot.Width, 320U);
  EXPECT_EQ(snapshot.Height, 200U);
  EXPECT_FLOAT_EQ(snapshot.SourceMaterial.roughness, 0.35F);
  EXPECT_FLOAT_EQ(snapshot.SourceMaterial.gtrGamma, 2.0F);
  EXPECT_FLOAT_EQ(snapshot.SourceMaterialProperties.Roughness, 0.35F);
  EXPECT_FLOAT_EQ(snapshot.SourceMaterialProperties.GtrGamma, 2.0F);
}

TEST(BrdfAnalysisSnapshotTests, ReportsSequentialProgressAndDiscardsResultsOnCancellation) {
  const BrdfAnalysisSnapshot snapshot = MakeSnapshot();
  const std::array models{Engine::Renderer::BrdfModel::GGX, Engine::Renderer::BrdfModel::GTR,
                          Engine::Renderer::BrdfModel::Beckmann};
  std::vector<size_t> completedCounts;
  size_t cancellationChecks = 0;
  Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureOptions options;
  options.Progress = [&completedCounts](size_t completed, size_t total, Engine::Renderer::BrdfModel) {
    EXPECT_EQ(total, 3U);
    completedCounts.push_back(completed);
  };
  options.IsCancelled = [&cancellationChecks] { return ++cancellationChecks >= 4U; };
  size_t rendered = 0;
  const auto result = CaptureBrdfAnalysisVariants(
      snapshot, models,
      [&rendered](const auto& request) -> Engine::Result<Engine::RenderOutput> {
        ++rendered;
        return MakeOutput(request.Width, request.Height);
      },
      options);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->Status, Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureResult::State::Cancelled);
  EXPECT_TRUE(result->Variants.empty());
  EXPECT_EQ(rendered, 2U);
  EXPECT_EQ(completedCounts, (std::vector<size_t>{1U}));
}

TEST(BrdfAnalysisSnapshotTests, RejectsMalformedOutputWithoutReturningPartialVariants) {
  const BrdfAnalysisSnapshot snapshot = MakeSnapshot();
  const std::array models{Engine::Renderer::BrdfModel::GGX, Engine::Renderer::BrdfModel::GTR};
  size_t rendered = 0;
  const auto result = CaptureBrdfAnalysisVariants(
      snapshot, models, [&rendered](const auto& request) -> Engine::Result<Engine::RenderOutput> {
        ++rendered;
        if (rendered == 1) {
          return MakeOutput(request.Width, request.Height);
        }
        std::vector<Engine::RenderOutputPlane> partial;
        return Engine::RenderOutput::CreateFromPlanes(request.Width, request.Height, std::move(partial));
      });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(rendered, 2U);
}

TEST(BrdfAnalysisSnapshotTests, CaptureSessionAdvancesOneModelAndOnlyPublishesCompleteResult) {
  const BrdfAnalysisSnapshot snapshot = MakeSnapshot();
  const std::array models{Engine::Renderer::BrdfModel::GGX, Engine::Renderer::BrdfModel::GTR};
  auto sessionResult = Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession::Create(snapshot, models);
  ASSERT_TRUE(sessionResult.has_value());
  auto session = std::move(*sessionResult);

  auto requestResult = session.GetNextRequest();
  ASSERT_TRUE(requestResult.has_value());
  ASSERT_NE(*requestResult, nullptr);
  EXPECT_EQ((*requestResult)->Model, models[0]);
  EXPECT_EQ(session.GetCompletedCount(), 0U);
  EXPECT_FALSE(session.TakeResult().has_value());
  ASSERT_TRUE(session.CompleteCurrent(*MakeOutput(320, 200)).has_value());
  EXPECT_EQ(session.GetCompletedCount(), 1U);

  requestResult = session.GetNextRequest();
  ASSERT_TRUE(requestResult.has_value());
  ASSERT_NE(*requestResult, nullptr);
  EXPECT_EQ((*requestResult)->Model, models[1]);
  ASSERT_TRUE(session.CompleteCurrent(*MakeOutput(320, 200)).has_value());
  EXPECT_EQ(session.GetStatus(), Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSessionState::Completed);
  requestResult = session.GetNextRequest();
  ASSERT_TRUE(requestResult.has_value());
  EXPECT_EQ(*requestResult, nullptr);

  auto result = session.TakeResult();
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->Variants.size(), 2U);
  EXPECT_EQ(result->Variants[0].Request.Model, models[0]);
  EXPECT_EQ(result->Variants[1].Request.Model, models[1]);
}

TEST(BrdfAnalysisSnapshotTests, CaptureSessionCancellationClearsPreviouslyCompletedVariants) {
  const BrdfAnalysisSnapshot snapshot = MakeSnapshot();
  const std::array models{Engine::Renderer::BrdfModel::GGX, Engine::Renderer::BrdfModel::GTR};
  auto sessionResult = Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession::Create(snapshot, models);
  ASSERT_TRUE(sessionResult.has_value());
  auto session = std::move(*sessionResult);
  ASSERT_TRUE(session.GetNextRequest().has_value());
  ASSERT_TRUE(session.CompleteCurrent(*MakeOutput(320, 200)).has_value());
  EXPECT_EQ(session.GetCompletedCount(), 1U);

  session.Cancel();
  EXPECT_EQ(session.GetStatus(), Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSessionState::Cancelled);
  EXPECT_EQ(session.GetCompletedCount(), 0U);
  auto result = session.TakeResult();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->Status, Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureResult::State::Cancelled);
  EXPECT_TRUE(result->Variants.empty());
}

TEST(BrdfAnalysisSnapshotTests, CaptureSessionFailureClearsPartialVariants) {
  const BrdfAnalysisSnapshot snapshot = MakeSnapshot();
  const std::array models{Engine::Renderer::BrdfModel::GGX, Engine::Renderer::BrdfModel::GTR};
  auto sessionResult = Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession::Create(snapshot, models);
  ASSERT_TRUE(sessionResult.has_value());
  auto session = std::move(*sessionResult);
  ASSERT_TRUE(session.GetNextRequest().has_value());
  ASSERT_TRUE(session.CompleteCurrent(*MakeOutput(320, 200)).has_value());
  ASSERT_TRUE(session.GetNextRequest().has_value());
  const Engine::Status failure = session.CompleteCurrent(*MakeOutput(160, 100));
  EXPECT_FALSE(failure.has_value());
  EXPECT_EQ(session.GetStatus(), Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSessionState::Failed);
  EXPECT_EQ(session.GetCompletedCount(), 0U);
  EXPECT_FALSE(session.TakeResult().has_value());
}

} // namespace
