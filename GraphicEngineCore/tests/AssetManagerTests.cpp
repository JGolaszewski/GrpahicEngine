#include "assets/include/AssetManager.h"
#include "scene/include/Scene.h"
#include "rendering/include/renderer/Camera.h"
#include "rendering/include/renderer/Entity.h"
#include "rendering/include/renderer/Light.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
void Require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class TestWorkspace
{
  public:
    TestWorkspace()
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        m_Path = std::filesystem::temp_directory_path() /
                 ("GraphicEngineAssetManagerTests-" + std::to_string(nonce));
        std::filesystem::create_directories(m_Path / "project");
    }
    ~TestWorkspace() noexcept
    {
        std::error_code ignored;
        try
        {
            std::filesystem::remove_all(m_Path, ignored);
        }
        catch (...)
        {
            std::fputs("Asset manager test workspace cleanup failed\n", stderr);
        }
    }

    const std::filesystem::path &Path() const noexcept
    {
        return m_Path;
    }
    std::filesystem::path Project() const
    {
        return m_Path / "project";
    }

  private:
    std::filesystem::path m_Path;
};

void WriteMinimalModel(const std::filesystem::path &path)
{
    const std::array<float, 9> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::array<uint16_t, 3> indices = {0, 1, 2};
    std::ofstream buffer(path.parent_path() / "triangle.bin", std::ios::binary | std::ios::trunc);
    buffer.write(reinterpret_cast<const char *>(positions.data()),
                 static_cast<std::streamsize>(sizeof(positions)));
    buffer.write(reinterpret_cast<const char *>(indices.data()),
                 static_cast<std::streamsize>(sizeof(indices)));
    Require(static_cast<bool>(buffer), "could not write triangle buffer fixture");

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output
        << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"root","mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"buffers":[{"uri":"triangle.bin","byteLength":42}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    Require(static_cast<bool>(output), "could not write minimal glTF fixture");
}

void WriteMinimalPng(const std::filesystem::path &path)
{
    constexpr std::array<uint8_t, 68> png = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00,
        0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x02, 0x00, 0x00, 0x05, 0x00,
        0x01, 0xa5, 0x2b, 0x56, 0x7b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    Require(static_cast<bool>(output), "could not write PNG fixture");
}

void TestTextureImportAndRegistry(const std::filesystem::path &project)
{
    WriteMinimalPng(project / "color.png");
    Engine::AssetManager first(project);
    std::string guid, error;
    Require(first.ImportTexture("color.png", guid, error), "texture import failed");
    Require(std::filesystem::exists(project / "color.png.meta"),
            "texture import did not write adjacent metadata");
    const auto image = first.GetTexture(guid);
    Require(image && image->width == 1 && image->height == 1 && image->rgba8.size() == 4,
            "texture import did not decode RGBA pixels");

    Engine::AssetManager reopened(project);
    std::vector<std::string> warnings;
    Require(reopened.RefreshRegistry(warnings, error), "asset registry refresh failed");
    Require(warnings.empty(), "valid texture metadata produced registry warnings");
    Require(reopened.GetTexture(guid) && reopened.GetKnownTextures().size() == 1,
            "texture was not discovered after reopening the project");

    std::filesystem::copy_file(project / "color.png.meta", project / "copied.png.meta");
    WriteMinimalPng(project / "copied.png");
    Engine::AssetManager duplicate(project);
    Require(!duplicate.ImportTexture("copied.png", guid, error) &&
                error.find("duplicate asset GUID") != std::string::npos,
            "copied texture metadata did not report the duplicate GUID");
}

void TestImportAndGuidReload(const std::filesystem::path &project)
{
    WriteMinimalModel(project / "model.gltf");
    Engine::AssetManager first(project);
    std::string guid;
    std::string error;
    Require(first.ImportModel("model.gltf", guid, error), "model import failed");
    Require(guid.size() == 36, "import did not create a UUID sidecar");
    Require(std::filesystem::exists(project / "model.gltf.meta"), "import did not write adjacent metadata");
    const auto imported = first.GetModel(guid);
    Require(imported != nullptr, "imported model was not retrievable by GUID");
    Require(imported->meshes.size() == 1 && imported->meshes[0].primitives.size() == 1,
            "glTF mesh hierarchy was not imported");
    const auto &primitive = imported->meshes[0].primitives[0];
    Require(primitive.vertices.size() == 3 && primitive.indices == std::vector<uint32_t>({0, 1, 2}),
            "glTF primitive geometry was not decoded correctly");
    Require(primitive.primitiveType == Engine::ImportedPrimitiveType::Triangles,
            "glTF triangle topology was not preserved as a backend-neutral asset type");
    Require(primitive.boundsMin.x == 0.0f && primitive.boundsMax.x == 1.0f && primitive.boundsMax.y == 1.0f,
            "glTF primitive bounds were not calculated correctly");
    Require(imported->scenes.size() == 1 && imported->scenes[0] == std::vector<uint32_t>({0}),
            "glTF selected scene roots were not preserved");
    Require(first.GetSourcePath(guid).has_value(), "source path was not registered");

    Engine::AssetManager reloaded(project);
    Require(reloaded.LoadModel(guid, error), "model GUID reload failed");
    const auto loaded = reloaded.GetModel(guid);
    Require(loaded != nullptr, "GUID reload did not populate the CPU model cache");
    Require(reloaded.GetSourcePath(guid) == first.GetSourcePath(guid),
            "GUID reload resolved a different source path");
    Require(reloaded.LoadModel(guid, error) && reloaded.GetModel(guid) == loaded,
            "repeated GUID loads did not reuse the cached CPU asset");
}

void TestDuplicateGuidAndProjectBoundary(const std::filesystem::path &workspace,
                                         const std::filesystem::path &project)
{
    const auto source = project / "model.gltf";
    WriteMinimalModel(source);
    Engine::AssetManager importer(project);
    std::string guid;
    std::string error;
    Require(importer.ImportModel(source, guid, error), "model import for duplicate-GUID test failed");
    std::filesystem::copy_file(source, project / "copy.gltf");
    std::filesystem::copy_file(project / "model.gltf.meta", project / "copy.gltf.meta");

    Engine::AssetManager freshRegistry(project);
    Require(!freshRegistry.LoadModel(guid, error), "duplicate GUID sidecars were not rejected");
    Require(error.find("duplicate asset GUID") != std::string::npos,
            "duplicate GUID error lacks the conflicting identity");

    Engine::AssetManager boundary(project);
    Require(!boundary.ImportModel(workspace / "outside.gltf", guid, error),
            "asset outside the project root was accepted");
}

void TestSceneJsonRoundTrip(const std::filesystem::path &project)
{
    Engine::AssetManager assets(project);
    Engine::SceneDocument document;
    document.unitsPerMeter = 0.01f;
    Engine::SceneEntityDocument entity;
    entity.id = 17;
    entity.name = "serialized entity";
    entity.modelGuid = "e6f7e48a-1d3b-4f65-9d61-5c78d4413001";
    Engine::SceneMaterialDocument overrideMaterial;
    overrideMaterial.baseColor = {0.2f, 0.4f, 0.6f, 1.0f};
    overrideMaterial.metallic = 0.7f;
    overrideMaterial.brdfModel = "ggx";
    overrideMaterial.baseColorTextureGuid = "00000000-0000-4000-8000-000000000001";
    overrideMaterial.normalTextureGuid = "00000000-0000-4000-8000-000000000002";
    entity.materialOverrides.push_back(overrideMaterial);
    entity.position = {1.0f, 2.0f, 3.0f};
    entity.forceWireframe = true;
    document.entities.push_back(entity);

    Engine::SceneEntityDocument primitive;
    primitive.id = 23;
    primitive.name = "editable primitive";
    primitive.primitiveType = "sphere";
    Engine::SceneMaterialDocument primitiveMaterial;
    primitiveMaterial.baseColor = {0.8f, 0.3f, 0.1f, 1.0f};
    primitiveMaterial.roughness = 0.25f;
    primitive.material = primitiveMaterial;
    document.entities.push_back(primitive);

    Engine::SceneLightDocument light;
    light.id = 31;
    light.position = {3.0f, 4.0f, 5.0f};
    document.lights.push_back(light);
    Engine::SceneCameraDocument camera;
    camera.id = 41;
    camera.position = {6.0f, 7.0f, 8.0f};
    document.cameras.push_back(camera);

    std::string error;
    const bool saved = assets.SaveScene("roundtrip.scene.json", document, error);
    Require(saved, "scene save failed");
    const auto loaded = assets.LoadScene("roundtrip.scene.json");
    Require(static_cast<bool>(loaded), "scene load failed");
    Require(loaded.document.entities.size() == 2, "scene round-trip lost an entity");
    const auto &result = loaded.document.entities.front();
    Require(result.id == entity.id && result.name == entity.name,
            "scene round-trip changed durable entity data");
    Require(result.modelGuid == entity.modelGuid, "scene round-trip changed an asset GUID");
    Require(result.materialOverrides.size() == 1 && result.materialOverrides[0].has_value(),
            "scene round-trip lost a model material override");
    const Engine::SceneMaterialDocument loadedOverride =
        result.materialOverrides.empty()
            ? Engine::SceneMaterialDocument{}
            : result.materialOverrides.front().value_or(Engine::SceneMaterialDocument{});
    Require(loadedOverride.baseColor == overrideMaterial.baseColor &&
                loadedOverride.metallic == overrideMaterial.metallic &&
                loadedOverride.baseColorTextureGuid == overrideMaterial.baseColorTextureGuid &&
                loadedOverride.normalTextureGuid == overrideMaterial.normalTextureGuid,
            "scene round-trip changed an editable material override");
    Require(result.position == entity.position && result.forceWireframe,
            "scene round-trip changed entity settings");
    Require(loaded.document.unitsPerMeter == document.unitsPerMeter,
            "scene round-trip changed its physical unit scale");
    const auto &loadedPrimitive = loaded.document.entities[1];
    Require(loadedPrimitive.id == primitive.id && loadedPrimitive.primitiveType == primitive.primitiveType,
            "scene round-trip changed a primitive descriptor or durable ID");
    Require(loadedPrimitive.material && loadedPrimitive.material->baseColor == primitiveMaterial.baseColor &&
                loadedPrimitive.material->roughness == primitiveMaterial.roughness,
            "scene round-trip changed primitive material settings");
    Require(loaded.document.lights.size() == 1 && loaded.document.lights[0].id == light.id &&
                loaded.document.lights[0].position == light.position,
            "scene round-trip changed light state");
    Require(loaded.document.cameras.size() == 1 && loaded.document.cameras[0].id == camera.id &&
                loaded.document.cameras[0].position == camera.position,
            "scene round-trip changed camera state");

    Require(!assets.SaveScene("../escaped.scene.json", document, error),
            "scene save escaped the project root");
}

void TestStableSceneIds()
{
    Engine::Scene scene;
    auto entity = std::make_shared<::Engine::Renderer::Entity>(nullptr, nullptr);
    Require(scene.AddEntity(entity, 17) == 17, "scene did not accept a persisted entity ID");
    Require(scene.AddEntity(std::make_shared<::Engine::Renderer::Entity>(nullptr, nullptr)) == 18,
            "runtime entity ID did not advance past a restored ID");
    Require(scene.AddEntity(std::make_shared<::Engine::Renderer::Entity>(nullptr, nullptr), 17) ==
                Engine::InvalidEntityId,
            "scene accepted a duplicate entity ID");

    auto replacement = std::make_shared<::Engine::Renderer::Entity>(nullptr, nullptr);
    Require(scene.ReplaceEntity(17, replacement), "scene did not replace an entity by stable ID");
    Require(scene.FindEntity(17) == replacement && scene.GetEntityId(replacement.get()) == 17,
            "entity replacement did not preserve stable scene identity");
    Require(!scene.ReplaceEntity(999, std::make_shared<::Engine::Renderer::Entity>(nullptr, nullptr)),
            "scene replaced a missing entity ID");
    Require(!scene.ReplaceEntity(18, nullptr), "scene accepted a null replacement entity");

    Require(scene.AddLight(std::make_shared<::Engine::Renderer::SpotLight>(), 27) == 27,
            "scene did not accept a persisted light ID");
    Require(scene.AddLight(std::make_shared<::Engine::Renderer::SpotLight>()) == 28,
            "runtime light ID did not advance past a restored ID");
    Require(scene.AddLight(std::make_shared<::Engine::Renderer::SpotLight>(), 27) == Engine::InvalidLightId,
            "scene accepted a duplicate light ID");

    Require(scene.AddCamera(std::make_shared<::Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f),
                            37) == 37,
            "scene did not accept a persisted camera ID");
    Require(scene.AddCamera(std::make_shared<::Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f)) ==
                38,
            "runtime camera ID did not advance past a restored ID");
    Require(scene.AddCamera(std::make_shared<::Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f),
                            37) == Engine::InvalidCameraId,
            "scene accepted a duplicate camera ID");
}

void TestInvalidInputs(const std::filesystem::path &project)
{
    Engine::AssetManager assets(project);
    std::string guid;
    std::string error;
    {
        std::ofstream invalid(project / "broken.gltf", std::ios::binary | std::ios::trunc);
        invalid << "{not valid JSON";
    }
    Require(!assets.ImportModel("broken.gltf", guid, error), "malformed glTF was accepted");
    Require(error.find("broken.gltf") != std::string::npos,
            "malformed glTF diagnostic omitted its source path");
    Require(!std::filesystem::exists(project / "broken.gltf.meta"), "failed import wrote a GUID sidecar");

    {
        std::ofstream invalidJson(project / "broken.scene.json", std::ios::binary | std::ios::trunc);
        invalidJson << "{not valid JSON";
    }
    const auto malformedScene = assets.LoadScene("broken.scene.json");
    Require(!malformedScene, "malformed scene JSON was accepted");
    Require(malformedScene.error.find("broken.scene.json") != std::string::npos,
            "malformed scene diagnostic omitted its source path");

    {
        std::ofstream unsupported(project / "future.scene.json", std::ios::binary | std::ios::trunc);
        unsupported << R"({"schemaVersion":99})";
    }
    Require(!assets.LoadScene("future.scene.json"), "unsupported future scene schema was accepted");
}

void TestAssetRootMetadataAndResourceUriFailures(const std::filesystem::path &workspace,
                                                 const std::filesystem::path &project)
{
    std::string guid, error;
    Engine::AssetManager unconfigured;
    Require(!unconfigured.LoadModel("invalid", error) &&
                error.find("project root must be set") != std::string::npos,
            "unconfigured model loading did not report the missing project root");
    Require(!unconfigured.LoadTexture("invalid", error) &&
                error.find("project root must be set") != std::string::npos,
            "unconfigured texture loading did not report the missing project root");

    Engine::AssetManager invalidRoot;
    Require(!invalidRoot.SetProjectRoot(workspace / "missing-project", error) && !error.empty(),
            "missing project root was accepted");

    Engine::AssetManager assets(project);
    Require(!assets.LoadModel("not-a-guid", error) && error.find("invalid") != std::string::npos,
            "invalid model GUID was accepted");
    Require(!assets.LoadTexture("not-a-guid", error) && error.find("invalid") != std::string::npos,
            "invalid texture GUID was accepted");
    Require(!assets.LoadModel("00000000-0000-4000-8000-000000000099", error) &&
                error.find("not found in project metadata") != std::string::npos,
            "unknown model GUID did not report the missing registration");

    std::ofstream unsupported(project / "unsupported.obj", std::ios::binary | std::ios::trunc);
    unsupported << "not a glTF";
    unsupported.close();
    Require(!assets.ImportModel("unsupported.obj", guid, error) &&
                error.find("only .gltf and .glb") != std::string::npos,
            "unsupported model extension was accepted");

    const auto withUri = [&project](const std::string &name, const std::string &uri) {
        const auto path = project / name;
        WriteMinimalModel(path);
        std::ifstream input(path, std::ios::binary);
        std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const size_t uriPosition = json.find("triangle.bin");
        Require(uriPosition != std::string::npos, "minimal glTF fixture has no external URI");
        json.replace(uriPosition, std::string("triangle.bin").size(), uri);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << json;
        Require(static_cast<bool>(output), "could not write glTF URI fixture");
        return path;
    };
    withUri("incomplete-uri.gltf", "broken%2");
    Require(!assets.ImportModel("incomplete-uri.gltf", guid, error) &&
                error.find("incomplete percent escape") != std::string::npos,
            "incomplete external URI escape was accepted");
    withUri("invalid-uri-hex.gltf", "%GG");
    Require(!assets.ImportModel("invalid-uri-hex.gltf", guid, error) &&
                error.find("invalid percent escape") != std::string::npos,
            "invalid external URI hex escape was accepted");
    withUri("invalid-uri-low-hex.gltf", "%7G");
    Require(!assets.ImportModel("invalid-uri-low-hex.gltf", guid, error) &&
                error.find("invalid percent escape") != std::string::npos,
            "invalid low external URI hex digit was accepted");
    withUri("zero-uri-escape.gltf", "%00");
    Require(!assets.ImportModel("zero-uri-escape.gltf", guid, error) &&
                error.find("invalid percent escape") != std::string::npos,
            "external URI zero-byte escape was accepted");
    withUri("absolute-uri.gltf", "C:/outside.bin");
    Require(!assets.ImportModel("absolute-uri.gltf", guid, error) &&
                error.find("must be project-relative") != std::string::npos,
            "absolute external URI was accepted");
    withUri("encoded-escape-uri.gltf", "..%2Foutside.bin");
    Require(!assets.ImportModel("encoded-escape-uri.gltf", guid, error) &&
                error.find("outside the project root") != std::string::npos,
            "percent-encoded external URI traversal was accepted");
    withUri("valid-escaped-uri.gltf", "%74riangle.bin");
    Require(assets.ImportModel("valid-escaped-uri.gltf", guid, error),
            "valid external URI percent escape was rejected");
    std::filesystem::copy_file(project / "triangle.bin", project / "~-triangle.bin",
                               std::filesystem::copy_options::overwrite_existing);
    withUri("lowercase-hex-uri.gltf", "%7e-triangle.bin");
    Require(assets.ImportModel("lowercase-hex-uri.gltf", guid, error),
            "lowercase external URI hex digits were rejected");
    withUri("uppercase-hex-uri.gltf", "%7E-triangle.bin");
    Require(assets.ImportModel("uppercase-hex-uri.gltf", guid, error),
            "uppercase external URI hex digits were rejected");
    withUri("escaped-uri.gltf", "../outside.bin");
    Require(!assets.ImportModel("escaped-uri.gltf", guid, error) &&
                error.find("outside the project root") != std::string::npos,
            "external glTF URI outside the project root was accepted");

    {
        std::ofstream malformedGlb(project / "malformed-header.glb", std::ios::binary | std::ios::trunc);
        const std::array<uint32_t, 3> invalidHeader = {0, 2, 12};
        malformedGlb.write(reinterpret_cast<const char *>(invalidHeader.data()), sizeof(invalidHeader));
    }
    Require(!assets.ImportModel("malformed-header.glb", guid, error) &&
                error.find("malformed GLB header") != std::string::npos,
            "malformed GLB header was accepted by asset URI validation");
    {
        std::ofstream noJsonChunk(project / "no-json-chunk.glb", std::ios::binary | std::ios::trunc);
        const std::array<uint32_t, 5> headerOnly = {0x46546c67, 2, 20, 0, 0};
        noJsonChunk.write(reinterpret_cast<const char *>(headerOnly.data()), sizeof(headerOnly));
    }
    Require(!assets.ImportModel("no-json-chunk.glb", guid, error) &&
                error.find("contains no JSON chunk") != std::string::npos,
            "GLB without a JSON chunk was accepted by asset URI validation");

    WriteMinimalModel(project / "bad-metadata.gltf");
    {
        std::ofstream metadata(project / "bad-metadata.gltf.meta", std::ios::binary | std::ios::trunc);
        metadata << "{not valid JSON";
    }
    Require(!assets.ImportModel("bad-metadata.gltf", guid, error) &&
                error.find("invalid metadata") != std::string::npos,
            "malformed import metadata was accepted");

    std::vector<std::string> warnings;
    Require(!unconfigured.RefreshRegistry(warnings, error) &&
                error.find("project root must be set") != std::string::npos,
            "unconfigured registry refresh did not reject the missing project root");

    const auto registryRoot = workspace / "registry-project";
    std::filesystem::create_directories(registryRoot);
    const auto writeMetadata = [](const std::filesystem::path &path, const std::string &assetType,
                                  const std::string &assetGuid) {
        const std::string importer = assetType == "model" ? "tinygltf" : "stb_image";
        std::ofstream metadata(path, std::ios::binary | std::ios::trunc);
        metadata << "{\"schemaVersion\":1,\"assetType\":\"" << assetType << "\",\"guid\":\"" << assetGuid
                 << "\",\"importer\":\"" << importer << "\",\"importerVersion\":1}";
        Require(static_cast<bool>(metadata), "could not write registry metadata fixture");
    };
    constexpr const char *duplicateGuid = "11111111-1111-4111-8111-111111111111";
    WriteMinimalModel(registryRoot / "alpha.gltf");
    WriteMinimalModel(registryRoot / "duplicate.gltf");
    writeMetadata(registryRoot / "alpha.gltf.meta", "model", duplicateGuid);
    writeMetadata(registryRoot / "duplicate.gltf.meta", "model", duplicateGuid);
    writeMetadata(registryRoot / "unsupported.asset.meta", "material", duplicateGuid);
    writeMetadata(registryRoot / "invalid-guid.gltf.meta", "model", "not-a-guid");
    writeMetadata(registryRoot / "missing-source.gltf.meta", "model", "22222222-2222-4222-8222-222222222222");
    {
        std::ofstream malformed(registryRoot / "malformed.meta", std::ios::binary | std::ios::trunc);
        malformed << "{not valid JSON";
    }

    Engine::AssetManager registry(registryRoot);
    warnings.clear();
    Require(registry.RefreshRegistry(warnings, error),
            "registry refresh failed on recoverable metadata errors");
    Require(warnings.size() >= 4, "registry refresh omitted recoverable metadata warnings");
    const auto hasWarning = [&warnings](const std::string &needle) {
        return std::any_of(warnings.begin(), warnings.end(), [&needle](const std::string &warning) {
            return warning.find(needle) != std::string::npos;
        });
    };
    Require(hasWarning("duplicate asset GUID"), "registry refresh missed a duplicate asset GUID");
    Require(hasWarning("unsupported asset metadata type"), "registry refresh missed an unsupported type");
    Require(hasWarning("invalid GUID"), "registry refresh missed an invalid GUID");
    Require(hasWarning("invalid metadata"), "registry refresh missed malformed JSON metadata");
    Require(hasWarning("source file does not exist"), "registry refresh missed a missing asset source");
}
} // namespace

int main()
{
    try
    {
        TestWorkspace workspace;
        TestImportAndGuidReload(workspace.Project());
        TestTextureImportAndRegistry(workspace.Project());
        TestDuplicateGuidAndProjectBoundary(workspace.Path(), workspace.Project());
        TestSceneJsonRoundTrip(workspace.Project());
        TestStableSceneIds();
        TestInvalidInputs(workspace.Project());
        TestAssetRootMetadataAndResourceUriFailures(workspace.Path(), workspace.Project());
        return EXIT_SUCCESS;
    }
    catch (const std::exception &exception)
    {
        std::fprintf(stderr, "AssetManagerTests failed: %s\n", exception.what());
        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::fputs("AssetManagerTests failed with an unknown exception\n", stderr);
        return EXIT_FAILURE;
    }
}
