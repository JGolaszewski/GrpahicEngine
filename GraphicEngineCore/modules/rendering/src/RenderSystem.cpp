#include "rendering/include/RenderSystem.h"
#include "assets/include/AssetManager.h"
#include "rendering/include/renderer/ModelResourceLibrary.h"
#include "rendering/src/MaterialSystem.h"
#include "rendering/src/ResourceManager.h"
#include "rendering/src/SceneRenderer.h"
#include "rendering/src/ShaderFactory.h"
#include "rendering/src/render_graph/FrameContext.h"
#include "rendering/src/render_graph/RenderGraph.h"
#include "rendering/src/render_graph/RenderPass.h"
#include "runtime/include/LogMacros.h"

#include <algorithm>
#include <nvrhi/nvrhi.h>
#include <utility>

namespace Engine {
namespace {
auto GetModelSceneRoots(const ImportedModel& model, std::string& error) -> std::optional<std::vector<uint32_t>> {
  if (model.defaultScene >= 0) {
    if (static_cast<size_t>(model.defaultScene) >= model.scenes.size()) {
      error = "model default scene index is invalid";
      return std::nullopt;
    }
    return model.scenes.at(static_cast<size_t>(model.defaultScene));
  }
  if (!model.scenes.empty()) {
    return model.scenes.front();
  }
  std::vector<uint32_t> roots;
  for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
    if (model.nodes.at(nodeIndex).parentIndex < 0) {
      roots.push_back(static_cast<uint32_t>(nodeIndex));
    }
  }
  return roots;
}

auto PrimitiveResourceKey(Engine::PrimitiveShape shape) -> const char* {
  switch (shape) {
  case Engine::PrimitiveShape::Cube:
    return "primitive:cube";
  case Engine::PrimitiveShape::Sphere:
    return "primitive:sphere";
  case Engine::PrimitiveShape::Plane:
    return "primitive:plane";
  case Engine::PrimitiveShape::Cone:
    return "primitive:cone";
  }
  return "primitive:unknown";
}
} // namespace

struct RenderSystem::CommandListHolder {
  nvrhi::CommandListHandle Handle;
};

namespace {
auto GetNvrhiDevice(IRenderBackend* backend) -> nvrhi::IDevice* {
  return backend == nullptr ? nullptr : static_cast<nvrhi::IDevice*>(backend->GetDevice());
}
} // namespace

auto CreateImmediateCommandList(IRenderBackend* backend) -> nvrhi::CommandListHandle {
  nvrhi::IDevice* device = GetNvrhiDevice(backend);
  if (device == nullptr) {
    return nullptr;
  }

  nvrhi::CommandListParameters parameters;
  parameters.setEnableImmediateExecution(true);
  return device->createCommandList(parameters);
}

RenderSystem::RenderSystem(IRenderBackend* backend)
    : m_RenderBackend(backend), m_CommandList(std::make_unique<CommandListHolder>()),
      m_FrameContext(std::make_unique<::Engine::Renderer::FrameContext>(GetNvrhiDevice(backend))),
      m_ModelResources(std::make_unique<::Engine::Renderer::ModelResourceLibrary>()) {
  m_CommandList->Handle = CreateImmediateCommandList(backend);
  ENGINE_LOG_INFO("Render", "Render system initialization started | backend_valid: {} | device_valid: {}",
                  backend != nullptr, GetNvrhiDevice(backend) != nullptr);
  if ((m_RenderBackend == nullptr) || (GetNvrhiDevice(m_RenderBackend) == nullptr)) {
    ENGINE_LOG_ERROR("Render", "Render system initialization failed | backend or device is null");
    return;
  }
  if (m_CommandList->Handle == nullptr) {
    ENGINE_LOG_ERROR("Render", "Render system initialization failed | command_list_valid: false");
    return;
  }

  m_ShaderFactory = std::make_unique<::Engine::Renderer::ShaderFactory>(GetNvrhiDevice(m_RenderBackend));
  auto m_DefaultVS = m_ShaderFactory->GetShader("shaders/color_vs.dxbc", nvrhi::ShaderType::Vertex);
  auto m_DefaultPS = m_ShaderFactory->GetShader("shaders/color_ps.dxbc", nvrhi::ShaderType::Pixel);
  auto m_TexturedPS = m_ShaderFactory->GetShader("shaders/color_textured_ps.dxbc", nvrhi::ShaderType::Pixel);

  m_ResourceManager = std::make_unique<::Engine::Renderer::ResourceManager>(GetNvrhiDevice(m_RenderBackend));
  m_MaterialSystem = std::make_unique<::Engine::Renderer::MaterialSystem>(
      GetNvrhiDevice(m_RenderBackend), m_DefaultVS, m_DefaultPS, m_ResourceManager->GetFallbackTexture(true),
      m_ResourceManager->GetFallbackTexture(false));
  m_MaterialSystem->SetTexturedPixelShader(m_TexturedPS);
  m_SceneRenderer = std::make_unique<SceneRenderer>(GetNvrhiDevice(m_RenderBackend));
  m_SceneRenderer->Init(GetNvrhiDevice(m_RenderBackend), m_MaterialSystem.get(), m_ShaderFactory.get(),
                        m_ModelResources.get());
  m_PrimitiveFactory =
      std::unique_ptr<Engine::PrimitiveFactory>(new Engine::PrimitiveFactory(GetNvrhiDevice(m_RenderBackend)));

  m_RenderGraph = std::make_unique<::Engine::Renderer::RenderGraph>(GetNvrhiDevice(m_RenderBackend), *m_FrameContext);
  ENGINE_LOG_INFO("Render", "Render system initialized | command_list_valid: {} | resource_manager_valid: {}",
                  m_CommandList->Handle != nullptr, m_ResourceManager != nullptr);
}

RenderSystem::~RenderSystem() = default;

bool RenderSystem::IsValid() const { return m_CommandList && m_CommandList->Handle != nullptr; }

auto RenderSystem::CreateViewportTarget(uint32_t width, uint32_t height) -> std::shared_ptr<Engine::ViewportTarget> {
  if ((m_RenderBackend == nullptr) || (GetNvrhiDevice(m_RenderBackend) == nullptr)) {
    return nullptr;
  }

  auto target =
      std::shared_ptr<Engine::ViewportTarget>(new Engine::ViewportTarget(m_RenderBackend->GetDevice(), width, height));
  return target->IsValid() ? target : nullptr;
}

auto RenderSystem::CreatePrimitive(Engine::PrimitiveShape shape, std::shared_ptr<::Engine::Renderer::Material> material)
    -> bool {
  if (m_FrameActive || !m_PrimitiveFactory) {
    ENGINE_LOG_ERROR("ECS", "Primitive creation rejected | shape: {} | frame_active: {} | factory_valid: {}",
                     static_cast<uint32_t>(shape), m_FrameActive, m_PrimitiveFactory != nullptr);
    return false;
  }

  auto submesh = m_PrimitiveFactory->CreatePrimitive(shape, std::move(material));
  if (submesh && submesh->mesh && submesh->material) {
    m_ModelResources->Register(PrimitiveResourceKey(shape), {*submesh});
    return true;
  }
  return false;
}

void RenderSystem::BeginFrame() {
  if (!IsValid()) {
    return;
  }
  m_FrameContext->BeginFrame();
  std::string uploadError;
  if (!m_ResourceManager->FlushUploads(uploadError)) {
    if (uploadError != m_LastUploadFailure) {
      ENGINE_LOG_ERROR("Render", "Queued GPU uploads failed | error: {}", uploadError);
    }
    m_LastUploadFailure = uploadError;
    return;
  }
  m_LastUploadFailure.clear();

  m_CommandList->Handle->open();
  m_FrameActive = true;
  m_RenderBackend->BeginGpuEvent(L"RenderFrame");
  ClearFrameBuffer();
  m_MaterialSystem->UpdateDirtyMaterials(m_CommandList->Handle);

  m_FrameContext->BeginGpuTiming(m_CommandList->Handle);

  m_RenderGraph->Reset();
}

void RenderSystem::SubmitViews(const std::vector<Engine::ViewportBinding>& views) {
  if (!m_FrameActive) {
    return;
  }
  ::Engine::Renderer::FrameBlackboard& bb = m_RenderGraph->GetBlackboard();

  std::vector<::Engine::Renderer::RenderView*> sceneViews;
  std::vector<Engine::Scene*> scenes;

  for (auto binding : views) {
    if (!binding.IsValid()) {
      continue;
    }

    auto view = std::make_unique<::Engine::Renderer::RenderView>();
    view->ViewType = ::Engine::Renderer::RenderView::Type::Main;
    view->Initialize(binding.TargetScene, binding.CameraEntity, binding.TargetViewport);
    view->TargetScene = binding.TargetScene;
    view->DebugSettings = binding.DebugSettings;

    sceneViews.push_back(view.get());
    m_FrameContext->GetViews().push_back(std::move(view));

    if (std::ranges::find(scenes, binding.TargetScene) == scenes.end()) {
      scenes.push_back(binding.TargetScene);
    }
  }
  bb.SetSceneViews(std::move(sceneViews));

  std::vector<::Engine::Renderer::RenderView*> shadowViews;
  shadowViews.reserve(scenes.size() * m_ShadowSettings.MaxShadowCastersPerScene);
  for (auto& scene : scenes) {
    const uint32_t firstShadowSlot = static_cast<uint32_t>(shadowViews.size());
    const uint32_t shadowCasterCount =
        ::Engine::SceneRenderer::CountShadowCasters(scene, m_ShadowSettings.MaxShadowCastersPerScene);
    for (uint32_t i = 0; i < shadowCasterCount; ++i) {
      auto view = std::make_unique<::Engine::Renderer::RenderView>();
      view->ViewType = ::Engine::Renderer::RenderView::Type::Shadow;
      shadowViews.push_back(view.get());
      m_FrameContext->GetViews().push_back(std::move(view));
    }
    ::Engine::SceneRenderer::BuildShadowData(scene, shadowViews, firstShadowSlot, shadowCasterCount);
  }
  bb.SetShadowViews(std::move(shadowViews));
  bb.Set<uint32_t>("ShadowMapSize", m_ShadowSettings.Resolution);
  bb.Set<uint32_t>("MaxShadowedLights", (std::max)(1U, static_cast<uint32_t>(bb.GetShadowViews().size())));

  ::Engine::Renderer::FrameDiagnostics& diagnostics = m_FrameContext->GetDiagnostics();
  m_SceneRenderer->BuildViews(bb, m_CommandList->Handle, *m_FrameContext, diagnostics);
  if (!::Engine::SceneRenderer::EnsureLightBuffers(bb.GetSceneViews().size(), *m_FrameContext)) {
    ENGINE_LOG_ERROR("Render", "Per-view light buffer allocation failed | view_count: {}", bb.GetSceneViews().size());
  }
  bb.SetPerObjectBuffer(m_SceneRenderer->GetPerObjectBuffer());
  bb.SetPerObjectBindingSet(m_SceneRenderer->GetPerObjectBindingSet());
  for (size_t viewIndex = 0; viewIndex < bb.GetSceneViews().size(); ++viewIndex) {
    ::Engine::Renderer::RenderView* view = bb.GetSceneViews().at(viewIndex);
    if (view != nullptr) {
      view->LightBuffer = ::Engine::SceneRenderer::GetLightBuffer(viewIndex, *m_FrameContext);
    }
    if ((view != nullptr) && (view->TargetScene != nullptr)) {
      ::Engine::SceneRenderer::BuildLighting(view->TargetScene, bb.GetShadowViews(), view->Lights);
    }
  }
  bb.SetShadowSampler(m_SceneRenderer->GetShadowSampler());
  bb.SetPerObjectBindingLayout(m_SceneRenderer->GetPerObjectBindingLayout());
  bb.SetSceneBindingLayout(m_SceneRenderer->GetSceneBindingLayout());

  m_RenderGraph->AddPass(std::make_unique<::Engine::Renderer::ShadowPass>());
  m_RenderGraph->AddPass(std::make_unique<::Engine::Renderer::ForwardPass>(m_SceneRenderer.get()));
  m_RenderGraph->AddPass(std::make_unique<::Engine::Renderer::DebugPass>(m_SceneRenderer.get()));
}

auto RenderSystem::SetShadowSettings(const ShadowSettings& settings) noexcept -> bool {
  if (settings.Resolution < 128 || settings.Resolution > 8192 ||
      settings.MaxShadowCastersPerScene > SceneRenderer::MaxShadowedLights) {
    ENGINE_LOG_ERROR("Render", "Shadow settings rejected | resolution: {} | max_shadow_casters: {}",
                     settings.Resolution, settings.MaxShadowCastersPerScene);
    return false;
  }

  m_ShadowSettings = settings;
  return true;
}

auto RenderSystem::GetOrCreateMesh(const std::string& guid, uint64_t generation,
                                   const std::vector<::Engine::Renderer::Vertex>& vertices,
                                   const std::vector<uint32_t>& indices, std::string& error)
    -> std::shared_ptr<::Engine::Renderer::Mesh> {
  if (m_FrameActive || !m_ResourceManager) {
    error = "GPU mesh creation is only available outside an active frame";
    ENGINE_LOG_ERROR("Render", "Mesh creation rejected | guid: {} | generation: {} | frame_active: {}", guid,
                     generation, m_FrameActive);
    return nullptr;
  }
  return m_ResourceManager->GetOrCreateMesh(guid, generation, vertices, indices, error);
}

auto RenderSystem::GetOrCreateTexture(const std::string& identity, uint64_t generation, const ImportedImage& image,
                                      bool srgb, std::string& error, const ImportedTexture* sampler)
    -> std::shared_ptr<::Engine::Renderer::TextureResource> {
  if (m_FrameActive || !m_ResourceManager) {
    error = "texture upload requires an initialized renderer outside an active frame";
    ENGINE_LOG_ERROR("Render", "Texture upload rejected | identity: {} | generation: {} | frame_active: {}", identity,
                     generation, m_FrameActive);
    return nullptr;
  }
  return m_ResourceManager->GetOrCreateTexture(identity, generation, image, srgb, error, sampler);
}

auto RenderSystem::CreateMaterial(const ::Engine::Renderer::MaterialProperties& properties)
    -> std::shared_ptr<::Engine::Renderer::Material> {
  if (m_FrameActive || !m_MaterialSystem) {
    ENGINE_LOG_ERROR("Render", "Material creation rejected | frame_active: {} | material_system_valid: {}",
                     m_FrameActive, m_MaterialSystem != nullptr);
    return nullptr;
  }
  auto material = m_MaterialSystem->CreateMaterial(properties);
  if (!material) {
    ENGINE_LOG_ERROR("Render", "Material allocation failed | roughness: {} | metallic: {} | workflow: {}",
                     properties.Roughness, properties.Metallic, static_cast<uint32_t>(properties.Workflow));
  }
  return material;
}

void RenderSystem::UnregisterMaterial(const std::shared_ptr<::Engine::Renderer::Material>& material) {
  if (!m_FrameActive && m_MaterialSystem) {
    m_MaterialSystem->UnregisterMaterial(material);
  }
}

auto RenderSystem::SetMaterialTextures(const std::shared_ptr<::Engine::Renderer::Material>& material,
                                       std::shared_ptr<::Engine::Renderer::TextureResource> baseColor,
                                       std::shared_ptr<::Engine::Renderer::TextureResource> normal,
                                       std::optional<std::string> baseColorGuid, std::optional<std::string> normalGuid,
                                       bool setImportedDefaults) -> bool {
  if (!m_FrameActive && m_MaterialSystem &&
      m_MaterialSystem->SetTextureBindings(material, std::move(baseColor), std::move(normal), std::move(baseColorGuid),
                                           std::move(normalGuid), setImportedDefaults)) {
    return true;
  }
  ENGINE_LOG_ERROR(
      "Render", "Material texture binding failed | material_valid: {} | frame_active: {} | material_system_valid: {}",
      material != nullptr, m_FrameActive, m_MaterialSystem != nullptr);
  return false;
}

auto RenderSystem::SetPbrMaterialTextures(const std::shared_ptr<::Engine::Renderer::Material>& material,
                                          std::shared_ptr<::Engine::Renderer::TextureResource> baseColor,
                                          std::shared_ptr<::Engine::Renderer::TextureResource> normal,
                                          std::shared_ptr<::Engine::Renderer::TextureResource> metallicRoughness,
                                          std::shared_ptr<::Engine::Renderer::TextureResource> occlusion,
                                          std::shared_ptr<::Engine::Renderer::TextureResource> emissive,
                                          bool setImportedDefaults) -> bool {
  if (!m_FrameActive && m_MaterialSystem &&
      m_MaterialSystem->SetPbrTextureBindings(material, std::move(baseColor), std::move(normal),
                                              std::move(metallicRoughness), std::move(occlusion), std::move(emissive),
                                              setImportedDefaults)) {
    return true;
  }
  ENGINE_LOG_ERROR("Render",
                   "PBR texture binding failed | material_valid: {} | frame_active: {} | material_system_valid: {}",
                   material != nullptr, m_FrameActive, m_MaterialSystem != nullptr);
  return false;
}

void RenderSystem::ReleaseResource(const std::string& guid) {
  if (m_ResourceManager) {
    m_ResourceManager->Release(guid);
  }
}

auto RenderSystem::UploadModel(const std::string& guid, uint64_t generation, const ImportedModel& model,
                               std::vector<std::vector<std::shared_ptr<::Engine::Renderer::Mesh>>>& meshes,
                               std::string& error) -> bool {
  if (m_FrameActive || !m_ResourceManager) {
    error = "GPU model uploads are only available outside an active frame";
    ENGINE_LOG_ERROR("Render", "Model upload rejected | guid: {} | generation: {} | frame_active: {}", guid, generation,
                     m_FrameActive);
    meshes.clear();
    return false;
  }
  return m_ResourceManager->GetOrCreateModelMeshes(guid, generation, model, meshes, error);
}

auto RenderSystem::CollectModelSubmeshes(const ImportedModel& model, std::vector<PendingModelSubmesh>& pending,
                                         std::string& error) -> bool {
  using namespace DirectX;
  std::vector<uint8_t> visited(model.nodes.size(), 0);
  const auto visitNode = [&](const auto& self, uint32_t nodeIndex, FXMMATRIX parent) -> bool {
    if (nodeIndex >= model.nodes.size() || visited.at(nodeIndex) != 0) {
      return false;
    }
    visited.at(nodeIndex) = 1;
    const auto& node = model.nodes.at(nodeIndex);
    XMMATRIX local{};
    if (node.hasMatrix) {
      local = XMLoadFloat4x4(&node.matrix);
    } else {
      local = XMMatrixAffineTransformation(XMLoadFloat3(&node.scale), XMVectorZero(), XMLoadFloat4(&node.rotation),
                                           XMLoadFloat3(&node.translation));
    }
    const XMMATRIX world = local * parent;
    if (node.meshIndex >= 0) {
      if (static_cast<size_t>(node.meshIndex) >= model.meshes.size()) {
        return false;
      }
      const auto& sourceMesh = model.meshes.at(static_cast<size_t>(node.meshIndex));
      for (size_t primitiveIndex = 0; primitiveIndex < sourceMesh.primitives.size(); ++primitiveIndex) {
        const auto& primitive = sourceMesh.primitives.at(primitiveIndex);
        XMFLOAT4X4 localTransform{};
        XMStoreFloat4x4(&localTransform, world);
        pending.push_back({
            .MeshIndex = static_cast<size_t>(node.meshIndex),
            .PrimitiveIndex = primitiveIndex,
            .MaterialIndex = primitive.materialIndex,
            .LocalTransform = localTransform,
            .BoundsMin = primitive.boundsMin,
            .BoundsMax = primitive.boundsMax,
        });
      }
    }
    return std::ranges::all_of(node.children,
                               [&self, &world](uint32_t child) -> bool { return self(self, child, world); });
  };

  const auto roots = GetModelSceneRoots(model, error);
  if (!roots) {
    return false;
  }
  for (const uint32_t root : *roots) {
    if (!visitNode(visitNode, root, XMMatrixIdentity())) {
      error = "model node hierarchy references invalid or repeated nodes";
      return false;
    }
  }
  if (pending.empty()) {
    error = "model's selected scene contains no renderable mesh primitives";
    return false;
  }
  return true;
}

void RenderSystem::UnregisterMaterials(const std::vector<std::shared_ptr<::Engine::Renderer::Material>>& materials) {
  for (const auto& material : materials) {
    m_MaterialSystem->UnregisterMaterial(material);
  }
}

auto RenderSystem::ResolveImportedTexture(const std::string& guid, uint64_t generation, const ImportedModel& model,
                                          int32_t textureIndex, bool srgb,
                                          std::shared_ptr<::Engine::Renderer::TextureResource>& destination,
                                          std::string& error) -> bool {
  if (textureIndex < 0) {
    return true;
  }
  if (static_cast<size_t>(textureIndex) >= model.textures.size()) {
    error = "imported material references an invalid texture slot";
    return false;
  }
  const auto& texture = model.textures.at(static_cast<size_t>(textureIndex));
  const int32_t imageIndex = texture.imageIndex;
  if (imageIndex < 0 || static_cast<size_t>(imageIndex) >= model.images.size()) {
    error = "imported texture references an invalid image slot";
    return false;
  }
  const auto& image = model.images.at(static_cast<size_t>(imageIndex));
  destination = m_ResourceManager->GetOrCreateTexture(guid + "/texture/" + std::to_string(textureIndex), generation,
                                                      image, srgb, error, &texture);
  return destination != nullptr;
}

auto RenderSystem::CreateImportedMaterial(
    const std::string& guid, uint64_t generation, const ImportedModel& model, size_t materialIndex,
    const std::vector<std::optional<::Engine::Renderer::MaterialProperties>>& overrides,
    std::shared_ptr<::Engine::Renderer::Material>& material, std::string& error) -> bool {
  const auto& sourceMaterial = model.materials.at(materialIndex);
  ::Engine::Renderer::MaterialProperties properties;
  const std::optional<::Engine::Renderer::MaterialProperties>* materialOverride =
      materialIndex < overrides.size() ? &overrides.at(materialIndex) : nullptr;
  if (materialOverride != nullptr && materialOverride->has_value()) {
    properties = materialOverride->value();
  } else {
    properties.BaseColor = sourceMaterial.baseColor;
    properties.Emissive = sourceMaterial.emissive;
    properties.Metallic = sourceMaterial.metallic;
    properties.Roughness =
        sourceMaterial.specularGlossiness ? 1.0F - sourceMaterial.glossiness : sourceMaterial.roughness;
    properties.Model = ::Engine::Renderer::BrdfModel::GGX;
    properties.Workflow = sourceMaterial.specularGlossiness ? ::Engine::Renderer::MaterialWorkflow::SpecularGlossiness
                                                            : ::Engine::Renderer::MaterialWorkflow::MetallicRoughness;
    if (sourceMaterial.specularGlossiness) {
      properties.SpecularF0 = sourceMaterial.specular;
    }
  }
  if (materialOverride == nullptr || !materialOverride->has_value()) {
    if (sourceMaterial.alphaMode == "MASK") {
      properties.AlphaMode = ::Engine::Renderer::MaterialAlphaMode::Mask;
    } else if (sourceMaterial.alphaMode == "BLEND") {
      properties.AlphaMode = ::Engine::Renderer::MaterialAlphaMode::Blend;
    } else {
      properties.AlphaMode = ::Engine::Renderer::MaterialAlphaMode::Opaque;
    }
    properties.AlphaCutoff = sourceMaterial.alphaCutoff;
    properties.DoubleSided = sourceMaterial.doubleSided;
  }
  properties.BaseColorTexCoord = sourceMaterial.baseColorTexCoord;
  properties.MetallicRoughnessTexCoord = sourceMaterial.metallicRoughnessTexCoord;
  properties.NormalTexCoord = sourceMaterial.normalTexCoord;
  properties.OcclusionTexCoord = sourceMaterial.occlusionTexCoord;
  properties.EmissiveTexCoord = sourceMaterial.emissiveTexCoord;
  properties.NormalScale = sourceMaterial.normalScale;
  properties.OcclusionStrength = sourceMaterial.occlusionStrength;
  const auto setTransform = [](::Engine::Renderer::MaterialTextureTransform& target,
                               const ImportedTextureTransform& source) -> void {
    target.Offset = source.offset;
    target.Scale = source.scale;
    target.Rotation = source.rotation;
  };
  setTransform(properties.TextureTransforms[0], sourceMaterial.baseColorTransform);
  setTransform(properties.TextureTransforms[1], sourceMaterial.normalTransform);
  setTransform(properties.TextureTransforms[2], sourceMaterial.metallicRoughnessTransform);
  setTransform(properties.TextureTransforms[3], sourceMaterial.occlusionTransform);
  setTransform(properties.TextureTransforms[4], sourceMaterial.emissiveTransform);

  material = m_MaterialSystem->CreateMaterial(properties);
  if (!material) {
    error = "renderer could not create a material for the imported model";
    return false;
  }
  auto baseColorTexture = m_ResourceManager->GetFallbackTexture(true);
  auto normalTexture = m_ResourceManager->GetFallbackTexture(false);
  auto metallicRoughnessTexture = m_ResourceManager->GetFallbackTexture(false);
  auto occlusionTexture = m_ResourceManager->GetFallbackTexture(false);
  auto emissiveTexture = m_ResourceManager->GetFallbackTexture(true);
  const bool texturesResolved =
      ResolveImportedTexture(guid, generation, model, sourceMaterial.baseColorTexture, true, baseColorTexture, error) &&
      ResolveImportedTexture(guid, generation, model, sourceMaterial.normalTexture, false, normalTexture, error) &&
      ResolveImportedTexture(guid, generation, model, sourceMaterial.metallicRoughnessTexture,
                             sourceMaterial.specularGlossiness, metallicRoughnessTexture, error) &&
      ResolveImportedTexture(guid, generation, model, sourceMaterial.occlusionTexture, false, occlusionTexture,
                             error) &&
      ResolveImportedTexture(guid, generation, model, sourceMaterial.emissiveTexture, true, emissiveTexture, error);
  const bool bindingsSet = texturesResolved && m_MaterialSystem->SetPbrTextureBindings(
                                                   material, baseColorTexture, normalTexture, metallicRoughnessTexture,
                                                   occlusionTexture, emissiveTexture, true);
  if (!bindingsSet) {
    m_MaterialSystem->UnregisterMaterial(material);
    material.reset();
    if (error.empty()) {
      error = "renderer could not bind imported material textures";
    }
    return false;
  }
  return true;
}

auto RenderSystem::CreateModelMaterials(
    const std::string& guid, uint64_t generation, const ImportedModel& model,
    const std::vector<std::optional<::Engine::Renderer::MaterialProperties>>& overrides,
    std::vector<std::shared_ptr<::Engine::Renderer::Material>>& materials, std::string& error) -> bool {
  materials.reserve(model.materials.size() + 1);
  for (size_t materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex) {
    std::shared_ptr<::Engine::Renderer::Material> material;
    if (!CreateImportedMaterial(guid, generation, model, materialIndex, overrides, material, error)) {
      UnregisterMaterials(materials);
      return false;
    }
    materials.push_back(std::move(material));
  }
  ::Engine::Renderer::MaterialProperties fallbackProperties;
  fallbackProperties.Model = ::Engine::Renderer::BrdfModel::GGX;
  auto fallback = m_MaterialSystem->CreateMaterial(fallbackProperties);
  if (!fallback) {
    UnregisterMaterials(materials);
    error = "renderer could not create a fallback model material";
    return false;
  }
  materials.push_back(std::move(fallback));
  return true;
}

auto RenderSystem::AssembleModelSubmeshes(
    const std::vector<std::vector<std::shared_ptr<::Engine::Renderer::Mesh>>>& meshes,
    const std::vector<PendingModelSubmesh>& pending,
    const std::vector<std::shared_ptr<::Engine::Renderer::Material>>& materials, std::string& error)
    -> std::optional<std::vector<::Engine::Renderer::Submesh>> {
  std::vector<::Engine::Renderer::Submesh> submeshes;
  submeshes.reserve(pending.size());
  for (const auto& source : pending) {
    const bool meshExists =
        source.MeshIndex < meshes.size() && source.PrimitiveIndex < meshes.at(source.MeshIndex).size();
    if (!meshExists) {
      UnregisterMaterials(materials);
      error = "uploaded mesh cache does not match imported model primitives";
      return std::nullopt;
    }
    const size_t materialIndex =
        source.MaterialIndex < 0 ? materials.size() - 1 : static_cast<size_t>(source.MaterialIndex);
    if (materialIndex >= materials.size()) {
      UnregisterMaterials(materials);
      error = "imported primitive references an invalid material slot";
      return std::nullopt;
    }
    ::Engine::Renderer::Submesh submesh;
    submesh.mesh = meshes.at(source.MeshIndex).at(source.PrimitiveIndex);
    submesh.material = materials.at(materialIndex);
    submesh.LocalTransform = source.LocalTransform;
    submesh.BoundsMin = source.BoundsMin;
    submesh.BoundsMax = source.BoundsMax;
    submesh.MaterialSlot = source.MaterialIndex;
    submeshes.push_back(std::move(submesh));
  }
  return submeshes;
}

auto RenderSystem::PrepareModelResources(
    const std::string& guid, uint64_t generation, const ImportedModel& model,
    const std::vector<std::optional<::Engine::Renderer::MaterialProperties>>& materialOverrides, std::string& error)
    -> bool {
  std::vector<PendingModelSubmesh> pending;
  if (!CollectModelSubmeshes(model, pending, error)) {
    return false;
  }

  std::vector<std::vector<std::shared_ptr<::Engine::Renderer::Mesh>>> meshes;
  if (!UploadModel(guid, generation, model, meshes, error)) {
    ENGINE_LOG_ERROR("Render", "Model GPU upload failed | guid: {} | generation: {} | error: {}", guid, generation,
                     error);
    return false;
  }

  std::vector<std::shared_ptr<::Engine::Renderer::Material>> materials;
  if (!CreateModelMaterials(guid, generation, model, materialOverrides, materials, error)) {
    return false;
  }

  auto submeshes = AssembleModelSubmeshes(meshes, pending, materials, error);
  if (submeshes) {
    m_ModelResources->Register(guid, *submeshes);
    return true;
  }
  return false;
}

void RenderSystem::EndFrame() {
  if (!IsValid() || !m_FrameActive) {
    return;
  }

  if (!m_RenderGraph->Compile()) {
    const std::string failure = "compile: " + m_RenderGraph->GetLastError();
    if (failure != m_LastRenderGraphFailure) {
      ENGINE_LOG_ERROR_CODE("Render", "render.graph.compile_failed", "Render graph compilation failed | error: {}",
                            failure);
    }
    m_LastRenderGraphFailure = failure;
    ClearSubmittedTargets();
    m_FrameContext->EndGpuTiming(m_CommandList->Handle);
    m_CommandList->Handle->close();
    GetNvrhiDevice(m_RenderBackend)->executeCommandList(m_CommandList->Handle);
    m_RenderBackend->EndGpuEvent();
    m_FrameActive = false;
    m_FrameContext->MarkSubmitted();
    m_ResourceManager->AdvanceSubmittedFrame(m_FrameContext->GetSubmittedFrames(),
                                             m_FrameContext->GetCompletedFrames());
    return;
  }
  if (m_LastRenderGraphFailure.starts_with("compile: ")) {
    m_LastRenderGraphFailure.clear();
  }
  if (!m_RenderGraph->Execute(m_CommandList->Handle)) {
    const std::string failure = "execute: " + m_RenderGraph->GetLastError();
    if (failure != m_LastRenderGraphFailure) {
      ENGINE_LOG_ERROR_CODE("Render", "render.graph.execute_failed", "Render graph execution failed | error: {}",
                            failure);
    }
    m_LastRenderGraphFailure = failure;
    ClearSubmittedTargets();
  } else {
    m_LastRenderGraphFailure.clear();
  }

  m_FrameContext->EndGpuTiming(m_CommandList->Handle);
  m_CommandList->Handle->close();
  GetNvrhiDevice(m_RenderBackend)->executeCommandList(m_CommandList->Handle);
  m_RenderBackend->EndGpuEvent();
  m_FrameActive = false;
  m_FrameContext->MarkSubmitted();
  m_ResourceManager->AdvanceSubmittedFrame(m_FrameContext->GetSubmittedFrames(), m_FrameContext->GetCompletedFrames());
}

auto RenderSystem::PickLight(float viewportX, float viewportY) const -> std::optional<Engine::LightId> {
  if (!m_SceneRenderer) {
    return std::nullopt;
  }

  for (const auto& view : m_FrameContext->GetViews()) {
    if (view && view->ViewType == ::Engine::Renderer::RenderView::Type::Main) {
      if (const auto lightId = ::Engine::SceneRenderer::PickLight(*view, viewportX, viewportY)) {
        return lightId;
      }
    }
  }

  return std::nullopt;
}

void RenderSystem::ClearFrameBuffer() {
  if (!IsValid()) {
    return;
  }

  auto* frameBuffer = static_cast<nvrhi::IFramebuffer*>(m_RenderBackend->GetDefaultFramebuffer());

  if (frameBuffer == nullptr) {
    return;
  }

  nvrhi::ITexture* colorTex = frameBuffer->getDesc().colorAttachments.at(0).texture;
  if (colorTex != nullptr) {
    m_CommandList->Handle->clearTextureFloat(colorTex, nvrhi::AllSubresources, nvrhi::Color(0.1F, 0.1F, 0.1F, 1.0F));
  }

  nvrhi::ITexture* depthTex = frameBuffer->getDesc().depthAttachment.texture;
  if (depthTex != nullptr) {
    m_CommandList->Handle->clearDepthStencilTexture(depthTex, nvrhi::AllSubresources, true, 1.0F, false, 0);
  }
}

void RenderSystem::ClearSubmittedTargets() {
  if (!IsValid() || !m_RenderGraph) {
    return;
  }

  for (::Engine::Renderer::RenderView* view : m_RenderGraph->GetBlackboard().GetSceneViews()) {
    if ((view == nullptr) || (view->TargetFramebuffer == nullptr)) {
      continue;
    }
    const nvrhi::FramebufferDesc& desc = view->TargetFramebuffer->getDesc();
    for (const nvrhi::FramebufferAttachment& attachment : desc.colorAttachments) {
      if (attachment.texture == nullptr) {
        continue;
      }
      m_CommandList->Handle->setTextureState(attachment.texture, attachment.subresources,
                                             nvrhi::ResourceStates::RenderTarget);
      m_CommandList->Handle->commitBarriers();
      m_CommandList->Handle->clearTextureFloat(attachment.texture, attachment.subresources,
                                               nvrhi::Color(0.15F, 0.0F, 0.15F, 1.0F));
    }
    if (desc.depthAttachment.valid() && (desc.depthAttachment.texture != nullptr)) {
      m_CommandList->Handle->setTextureState(desc.depthAttachment.texture, desc.depthAttachment.subresources,
                                             nvrhi::ResourceStates::DepthWrite);
      m_CommandList->Handle->commitBarriers();
      m_CommandList->Handle->clearDepthStencilTexture(desc.depthAttachment.texture, desc.depthAttachment.subresources,
                                                      true, 1.0F, false, 0);
    }
  }
}

const ::Engine::Renderer::ModelResourceLibrary& RenderSystem::GetModelResources() const noexcept {
  return *m_ModelResources;
}

const ::Engine::Renderer::FrameDiagnostics& RenderSystem::GetFrameDiagnostics() const noexcept {
  return m_FrameContext->GetDiagnostics();
}

::Engine::Renderer::GpuFrameTiming RenderSystem::GetGpuFrameTiming() const noexcept {
  return m_FrameContext->GetGpuFrameTiming();
}

uint32_t RenderSystem::GetSubmittedViewCount() const noexcept {
  return static_cast<uint32_t>(m_FrameContext->GetBlackboard().GetSceneViews().size() +
                               m_FrameContext->GetBlackboard().GetShadowViews().size());
}
} // namespace Engine
