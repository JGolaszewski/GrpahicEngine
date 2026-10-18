#include "rendering/include/renderer/brdf/BrdfAnalysisSnapshot.h"

#include <algorithm>
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
                          const Engine::SceneMaterialDocument& sourceMaterial, Engine::Renderer::BrdfModel model)
    -> Engine::Status {
  const auto entity = std::find_if(scene.entities.begin(), scene.entities.end(),
                                   [entityId](const auto& item) { return item.id == entityId; });
  if (entity == scene.entities.end()) {
    return std::unexpected(Engine::MakeError("BRDF analysis target entity was not found in the captured scene"));
  }

  Engine::SceneMaterialDocument material = sourceMaterial;
  material.brdfModel = ModelIdentifier(model);
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

} // namespace

auto Engine::Renderer::BrdfAnalysis::ExecuteBrdfAnalysisVariants(const BrdfAnalysisSnapshot& snapshot,
                                                                 std::span<const BrdfModel> models,
                                                                 const BrdfAnalysisRenderCallback& render)
    -> Engine::Result<std::vector<BrdfAnalysisVariant>> {
  if (snapshot.Width == 0 || snapshot.Height == 0) {
    return std::unexpected(Engine::MakeError("BRDF analysis render dimensions must be positive"));
  }
  if (models.empty()) {
    return std::unexpected(Engine::MakeError("BRDF analysis requires at least one model"));
  }
  if (!render) {
    return std::unexpected(Engine::MakeError("BRDF analysis render callback is not set"));
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

  std::vector<BrdfAnalysisVariant> variants;
  variants.reserve(models.size());
  for (const BrdfModel model : models) {
    BrdfAnalysisRenderRequest request;
    request.Scene = snapshot.Scene;
    request.Model = model;
    request.Material = snapshot.SourceMaterialProperties;
    request.Material.Model = model;
    request.Camera = snapshot.Camera;
    request.Width = snapshot.Width;
    request.Height = snapshot.Height;
    request.ShadowMapResolution = snapshot.ShadowMapResolution;
    request.MaxShadowCastersPerScene = snapshot.MaxShadowCastersPerScene;
    request.PostProcess = snapshot.PostProcess;

    const Engine::Status materialStatus = ApplyMaterialVariant(
        request.Scene, snapshot.TargetEntityId, snapshot.MaterialOverrideIndex, snapshot.SourceMaterial, model);
    if (!materialStatus) {
      return std::unexpected(materialStatus.error());
    }

    try {
      Engine::Result<Engine::RenderOutput> output = render(request);
      if (!output) {
        return std::unexpected(output.error());
      }
      if (output->Width != snapshot.Width || output->Height != snapshot.Height) {
        return std::unexpected(Engine::MakeError("BRDF analysis render output dimensions do not match the snapshot"));
      }
      variants.push_back(BrdfAnalysisVariant{.Request = std::move(request), .Output = std::move(*output)});
    } catch (const std::exception& exception) {
      return std::unexpected(
          Engine::MakeError(std::string("BRDF analysis render callback failed: ") + exception.what()));
    } catch (...) {
      return std::unexpected(Engine::MakeError("BRDF analysis render callback failed with an unknown exception"));
    }
  }
  return variants;
}
