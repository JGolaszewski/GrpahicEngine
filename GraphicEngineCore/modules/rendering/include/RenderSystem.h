#pragma once
#include "assets/include/AssetImporter.h"
#include "runtime/include/CoreExport.h"
#include "rendering/include/PrimitiveFactory.h"
#include "rendering/include/RenderLimits.h"
#include "rendering/include/RenderDiagnostics.h"
#include "rendering/include/ViewportTarget.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/include/renderer/ViewportBinding.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine
{
namespace Renderer
{
class Entity;
class Material;
class MaterialSystem;
class Mesh;
class ResourceManager;
class ShaderFactory;
struct TextureResource;
struct Vertex;
} // namespace Renderer

class IRenderBackend;

/// Quality and scene-complexity limits for shadow-map rendering.
struct ShadowSettings
{
    uint32_t Resolution = Engine::ShadowMapSize; ///< Shadow-map width and height in pixels.
    uint32_t MaxShadowCastersPerScene =
        Engine::MaxShadowedLights; ///< Maximum shadow-casting lights per scene.
};

/// CPU work counters collected while submitting render views.
struct RenderSubmissionStats
{
    uint32_t ObjectsSubmitted = 0;    ///< Objects presented to visibility processing.
    uint32_t ObjectsCulled = 0;       ///< Objects rejected by frustum or visibility checks.
    uint32_t PacketsBuilt = 0;        ///< Total render packets produced.
    uint32_t OpaquePackets = 0;       ///< Opaque render packets produced.
    uint32_t MaskedPackets = 0;       ///< Alpha-masked render packets produced.
    uint32_t TransparentPackets = 0;  ///< Transparent render packets produced.
    uint32_t PipelineCacheHits = 0;   ///< Pipeline lookups served from the cache.
    uint32_t PipelineCacheMisses = 0; ///< Pipeline lookups requiring creation.
    uint32_t PipelineCreations = 0;   ///< Graphics pipelines created during submission.
    double CullingTimeMs = 0.0;       ///< CPU time spent rejecting invisible objects.
    double PacketBuildTimeMs = 0.0;   ///< CPU time spent building draw packets.
};

/// Coordinates frame submission, resource lifetime, and renderer-facing scene work.
class ENGINE_API RenderSystem
{
  public:
    /// Creates the renderer service for an initialized backend.
    /// @param backend Backend that owns the graphics device and presentation target.
    RenderSystem(IRenderBackend *backend);

    /// Releases renderer-owned resources.
    ~RenderSystem();

    /// Reports whether the backend has a usable graphics device.
    /// @return True when rendering operations can be submitted.
    bool IsValid() const noexcept;

    /// Starts a render frame when the backend is valid.
    void BeginFrame();

    /// Submits views for culling, packet creation, and rendering.
    /// @param views Camera, viewport, and scene bindings to render.
    void SubmitViews(const std::vector<Engine::ViewportBinding> &views);

    /// Completes the current render frame.
    void EndFrame();

    /// Validates and applies shadow-map settings.
    /// @param settings Requested resolution and caster limit.
    /// @return True when the settings are supported and applied.
    bool SetShadowSettings(const ShadowSettings &settings) noexcept;

    /// Returns the current shadow-map settings.
    /// @return Const reference to the applied settings.
    const ShadowSettings &GetShadowSettings() const noexcept;

    /// Picks a scene light at viewport-relative coordinates using the last submitted frame.
    /// @param viewportX Horizontal coordinate in viewport pixels.
    /// @param viewportY Vertical coordinate in viewport pixels.
    /// @return Light identifier when a light is hit, otherwise empty.
    std::optional<Engine::LightId> PickLight(float viewportX, float viewportY) const;

    /// Creates a backend-owned render target.
    /// @param width Target width in pixels.
    /// @param height Target height in pixels.
    /// @return New target, or empty when creation fails.
    std::shared_ptr<Engine::ViewportTarget> CreateViewportTarget(uint32_t width, uint32_t height);

    /// Creates an entity backed by a built-in primitive mesh.
    /// @param shape Primitive shape to create.
    /// @param material Material assigned to the primitive.
    /// @param position Initial world-space position.
    /// @param scale Per-axis scale.
    /// @return New entity, or empty when mesh creation fails.
    std::shared_ptr<::Engine::Renderer::Entity> CreatePrimitive(
        Engine::PrimitiveShape shape, std::shared_ptr<::Engine::Renderer::Material> material,
        const DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT3 &scale = {1.0f, 1.0f, 1.0f});

    /// Returns a GPU mesh cached for an asset generation, creating it when needed.
    /// @param guid Stable asset identifier.
    /// @param generation Asset content generation.
    /// @param vertices CPU-side vertex data.
    /// @param indices CPU-side index data.
    /// @param error Receives a diagnostic when upload fails.
    /// @return Shared GPU mesh, or empty on failure.
    std::shared_ptr<::Engine::Renderer::Mesh> GetOrCreateMesh(
        const std::string &guid, uint64_t generation, const std::vector<::Engine::Renderer::Vertex> &vertices,
        const std::vector<uint32_t> &indices, std::string &error);
    /// Returns a GPU texture cached for an identity and generation, creating it when needed.
    /// @param identity Stable texture identity.
    /// @param generation Texture content generation.
    /// @param image Decoded CPU-side image.
    /// @param srgb True when color data requires sRGB sampling.
    /// @param error Receives a diagnostic when upload fails.
    /// @param sampler Optional imported sampling parameters.
    /// @return Shared GPU texture, or empty on failure.
    std::shared_ptr<::Engine::Renderer::TextureResource> GetOrCreateTexture(
        const std::string &identity, uint64_t generation, const ImportedImage &image, bool srgb,
        std::string &error, const ImportedTexture *sampler = nullptr);
    /// Creates a material using renderer default textures.
    /// @param properties Material factors and workflow settings.
    /// @return New material, or empty when the renderer is unavailable.
    std::shared_ptr<::Engine::Renderer::Material> CreateMaterial(
        const ::Engine::Renderer::MaterialProperties &properties);
    /// Removes a material from renderer tracking.
    /// @param material Material to unregister.
    void UnregisterMaterial(const std::shared_ptr<::Engine::Renderer::Material> &material);

    /// Assigns base-color and normal texture resources to a material.
    /// @param material Target material.
    /// @param baseColor Base-color texture resource.
    /// @param normal Normal texture resource.
    /// @param baseColorGuid Optional persistent base-color texture identifier.
    /// @param normalGuid Optional persistent normal texture identifier.
    /// @param setImportedDefaults Whether these resources become imported defaults.
    /// @return True when the material was updated.
    bool SetMaterialTextures(const std::shared_ptr<::Engine::Renderer::Material> &material,
                             std::shared_ptr<::Engine::Renderer::TextureResource> baseColor,
                             std::shared_ptr<::Engine::Renderer::TextureResource> normal,
                             std::optional<std::string> baseColorGuid = {},
                             std::optional<std::string> normalGuid = {}, bool setImportedDefaults = false);
    /// Assigns the full PBR texture set to a material.
    /// @param material Target material.
    /// @param baseColor Base-color texture resource.
    /// @param normal Normal texture resource.
    /// @param metallicRoughness Metallic-roughness texture resource.
    /// @param occlusion Occlusion texture resource.
    /// @param emissive Emissive texture resource.
    /// @param setImportedDefaults Whether these resources become imported defaults.
    /// @return True when the material was updated.
    bool SetPbrMaterialTextures(const std::shared_ptr<::Engine::Renderer::Material> &material,
                                std::shared_ptr<::Engine::Renderer::TextureResource> baseColor,
                                std::shared_ptr<::Engine::Renderer::TextureResource> normal,
                                std::shared_ptr<::Engine::Renderer::TextureResource> metallicRoughness,
                                std::shared_ptr<::Engine::Renderer::TextureResource> occlusion,
                                std::shared_ptr<::Engine::Renderer::TextureResource> emissive,
                                bool setImportedDefaults = false);
    /// Releases cached GPU objects associated with an asset identity.
    /// @param guid Asset identity to invalidate.
    void ReleaseResource(const std::string &guid);

    /// Reports whether a render frame is currently active.
    /// @return True between a successful BeginFrame and EndFrame.
    bool IsFrameActive() const noexcept;

    /// Uploads imported model meshes and materials for a content generation.
    /// @param guid Stable model asset identifier.
    /// @param generation Model content generation.
    /// @param model Imported CPU-side model data.
    /// @param meshes Receives uploaded meshes grouped by source mesh.
    /// @param error Receives a diagnostic when upload fails.
    /// @return True when all required model resources are available.
    bool UploadModel(const std::string &guid, uint64_t generation, const ImportedModel &model,
                     std::vector<std::vector<std::shared_ptr<::Engine::Renderer::Mesh>>> &meshes,
                     std::string &error);
    /// Creates a render entity from an imported model and optional material overrides.
    /// @param guid Stable model asset identifier.
    /// @param generation Model content generation.
    /// @param model Imported CPU-side model data.
    /// @param materialOverrides Per-material optional property overrides.
    /// @param error Receives a fatal creation diagnostic.
    /// @param warnings Receives recoverable asset or material warnings.
    /// @return New entity, or empty when creation fails.
    std::shared_ptr<::Engine::Renderer::Entity> CreateModelEntity(
        const std::string &guid, uint64_t generation, const ImportedModel &model,
        const std::vector<std::optional<::Engine::Renderer::MaterialProperties>> &materialOverrides,
        std::string &error, std::vector<std::string> &warnings);

    /// Returns counters from the most recent view submission.
    /// @return Copy of submission counters.
    RenderSubmissionStats GetSubmissionStats() const noexcept;

    /// Returns backend-neutral diagnostics for the most recently rendered frame.
    /// @return Const reference to frame diagnostics.
    const ::Engine::Renderer::FrameDiagnostics &GetFrameDiagnostics() const noexcept;

    /// Returns the latest asynchronous GPU frame timing sample.
    /// @return GPU timing availability, frame index, and duration.
    ::Engine::Renderer::GpuFrameTiming GetGpuFrameTiming() const noexcept;

    /// Returns the number of views submitted in the current or most recent frame.
    /// @return Submitted view count.
    uint32_t GetSubmittedViewCount() const noexcept;

  private:
    void ClearFrameBuffer();
    void ClearSubmittedTargets();

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
} // namespace Engine

#ifdef _MSC_VER
#pragma warning(pop)
#endif
