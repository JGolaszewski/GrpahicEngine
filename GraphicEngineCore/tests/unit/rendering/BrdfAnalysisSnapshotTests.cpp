#include "rendering/include/renderer/brdf/BrdfAnalysisSnapshot.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Engine::Renderer::BrdfAnalysis::BrdfAnalysisSnapshot;
using Engine::Renderer::BrdfAnalysis::BrdfAnalysisVariant;
using Engine::Renderer::BrdfAnalysis::ExecuteBrdfAnalysisVariants;

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

    Engine::RenderOutput output;
    output.Width = request.Width;
    output.Height = request.Height;
    return output;
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
    Engine::RenderOutput output;
    output.Width = 320;
    output.Height = 200;
    return output;
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
    return Engine::RenderOutput{};
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
    Engine::RenderOutput output;
    output.Width = request.Width;
    output.Height = request.Height;
    return output;
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
    Engine::RenderOutput output;
    output.Width = request.Width;
    output.Height = request.Height;
    return output;
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

} // namespace
