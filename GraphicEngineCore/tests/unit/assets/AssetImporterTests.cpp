#include "assets/include/AssetImporter.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Json = nlohmann::json;

class ImportWorkspace
{
  public:
    ImportWorkspace()
    {
        m_Path = std::filesystem::temp_directory_path() /
                 ("GraphicEngineImporterTests_" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(m_Path);
    }
    ~ImportWorkspace()
    {
        try
        {
            std::error_code error;
            std::filesystem::remove_all(m_Path, error);
        }
        catch (...)
        {
            std::fputs("Failed to clean asset importer test workspace.\n", stderr);
        }
    }
    const std::filesystem::path &Path() const
    {
        return m_Path;
    }

  private:
    std::filesystem::path m_Path;
};

Json MinimalModel()
{
    return {
        {"asset", {{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", Json::array({{{"nodes", Json::array({0})}}})},
        {"nodes", Json::array({{{"name", "triangle"}, {"mesh", 0}}})},
        {"meshes",
         Json::array({{{"primitives",
                        Json::array({{{"attributes", {{"POSITION", 0}}}, {"indices", 1}, {"mode", 4}}})}}})},
        {"buffers", Json::array({{{"uri", "triangle.bin"}, {"byteLength", 42}}})},
        {"bufferViews", Json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
                                     {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 6}}})},
        {"accessors",
         Json::array({{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
                      {{"bufferView", 1}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}}})}};
}

void WriteModelWithBinary(const std::filesystem::path &path, const Json &model,
                          const std::vector<uint8_t> &bytes)
{
    std::ofstream buffer(path.parent_path() / "triangle.bin", std::ios::binary | std::ios::trunc);
    buffer.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(buffer.good());

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << model.dump(2);
    ASSERT_TRUE(output.good());
}

void WriteModel(const std::filesystem::path &path, const Json &model)
{
    const std::array<float, 9> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::array<uint16_t, 3> indices = {0, 1, 2};
    std::vector<uint8_t> bytes(sizeof(positions) + sizeof(indices));
    std::memcpy(bytes.data(), positions.data(), sizeof(positions));
    std::memcpy(bytes.data() + sizeof(positions), indices.data(), sizeof(indices));
    WriteModelWithBinary(path, model, bytes);
}

void WritePng(const std::filesystem::path &path)
{
    constexpr std::array<uint8_t, 68> bytes = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00,
        0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x02, 0x00, 0x00, 0x05, 0x00,
        0x01, 0xa5, 0x2b, 0x56, 0x7b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteGlb(const std::filesystem::path &path)
{
    Json model = MinimalModel();
    model["buffers"] = Json::array({{{"byteLength", 42}}});
    std::string json = model.dump();
    while (json.size() % 4 != 0)
        json.push_back(' ');

    const std::array<float, 9> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::array<uint16_t, 3> indices = {0, 1, 2};
    std::vector<uint8_t> binary(sizeof(positions) + sizeof(indices));
    std::memcpy(binary.data(), positions.data(), sizeof(positions));
    std::memcpy(binary.data() + sizeof(positions), indices.data(), sizeof(indices));
    while (binary.size() % 4 != 0)
        binary.push_back(0);

    std::vector<uint8_t> bytes;
    const auto append32 = [&bytes](uint32_t value) {
        bytes.push_back(static_cast<uint8_t>(value & 0xff));
        bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
        bytes.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    };
    const uint32_t totalLength = static_cast<uint32_t>(12 + 8 + json.size() + 8 + binary.size());
    append32(0x46546c67);
    append32(2);
    append32(totalLength);
    append32(static_cast<uint32_t>(json.size()));
    append32(0x4e4f534a);
    bytes.insert(bytes.end(), json.begin(), json.end());
    append32(static_cast<uint32_t>(binary.size()));
    append32(0x004e4942);
    bytes.insert(bytes.end(), binary.begin(), binary.end());

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

TEST(AssetImporterTests, RejectsUnsupportedPathsMalformedFilesAndExtensions)
{
    ImportWorkspace workspace;
    Engine::AssetImporter importer;

    const auto unsupported = importer.ImportGltf(workspace.Path() / "model.obj");
    EXPECT_FALSE(static_cast<bool>(unsupported));
    EXPECT_NE(unsupported.error.find("expected a .gltf or .glb"), std::string::npos);

    const auto malformedPath = workspace.Path() / "malformed.gltf";
    {
        std::ofstream output(malformedPath);
        output << "{not valid JSON";
    }
    const auto malformed = importer.ImportGltf(malformedPath);
    EXPECT_FALSE(static_cast<bool>(malformed));
    EXPECT_NE(malformed.error.find("malformed.gltf"), std::string::npos);

    Json model = MinimalModel();
    model["extensionsRequired"] = Json::array({"KHR_vendor_required"});
    const auto requiredExtensionPath = workspace.Path() / "required-extension.gltf";
    WriteModel(requiredExtensionPath, model);
    const auto requiredExtension = importer.ImportGltf(requiredExtensionPath);
    EXPECT_FALSE(static_cast<bool>(requiredExtension));
    EXPECT_NE(requiredExtension.error.find("required glTF extension is unsupported"), std::string::npos);

    model = MinimalModel();
    model["extensionsUsed"] = Json::array({"KHR_vendor_optional"});
    const auto optionalExtensionPath = workspace.Path() / "optional-extension.gltf";
    WriteModel(optionalExtensionPath, model);
    const auto optionalExtension = importer.ImportGltf(optionalExtensionPath);
    ASSERT_TRUE(static_cast<bool>(optionalExtension)) << optionalExtension.error;
    ASSERT_EQ(optionalExtension.warnings.size(), 1U);
    EXPECT_NE(optionalExtension.warnings.front().find("will be ignored"), std::string::npos);
}

TEST(AssetImporterTests, ConvertsPrimitiveTopologiesAndGeneratesMissingAttributes)
{
    ImportWorkspace workspace;
    Engine::AssetImporter importer;
    const std::array<std::pair<int, Engine::ImportedPrimitiveType>, 5> topologies = {{
        {0, Engine::ImportedPrimitiveType::Points},
        {2, Engine::ImportedPrimitiveType::Lines},
        {3, Engine::ImportedPrimitiveType::LineStrip},
        {5, Engine::ImportedPrimitiveType::Triangles},
        {6, Engine::ImportedPrimitiveType::Triangles},
    }};

    for (const auto &[mode, expected] : topologies)
    {
        Json model = MinimalModel();
        model["meshes"][0]["primitives"][0]["mode"] = mode;
        const auto path = workspace.Path() / ("topology-" + std::to_string(mode) + ".gltf");
        WriteModel(path, model);
        const auto result = importer.ImportGltf(path);
        ASSERT_TRUE(static_cast<bool>(result)) << result.error;
        const auto &primitive = result.model.meshes[0].primitives[0];
        EXPECT_EQ(primitive.primitiveType, expected);
        if (mode == 2)
            EXPECT_EQ(primitive.indices, std::vector<uint32_t>({0, 1, 1, 2, 2, 0}));
        if (mode == 5)
            EXPECT_EQ(primitive.indices, std::vector<uint32_t>({0, 1, 2}));
    }

    Json unindexed = MinimalModel();
    unindexed["meshes"][0]["primitives"][0].erase("indices");
    const auto unindexedPath = workspace.Path() / "unindexed.gltf";
    WriteModel(unindexedPath, unindexed);
    const auto unindexedResult = importer.ImportGltf(unindexedPath);
    ASSERT_TRUE(static_cast<bool>(unindexedResult)) << unindexedResult.error;
    const auto &generated = unindexedResult.model.meshes[0].primitives[0];
    EXPECT_EQ(generated.indices, std::vector<uint32_t>({0, 1, 2}));
    EXPECT_FLOAT_EQ(generated.vertices[0].Normal.z, 1.0f);
    EXPECT_FLOAT_EQ(generated.vertices[0].Tangent.w, 1.0f);
}

TEST(AssetImporterTests, RejectsInvalidPrimitiveAndHierarchyData)
{
    ImportWorkspace workspace;
    Engine::AssetImporter importer;
    const auto expectError = [&](const std::string &name, const Json &model, const std::string &message) {
        const auto path = workspace.Path() / (name + ".gltf");
        WriteModel(path, model);
        const auto result = importer.ImportGltf(path);
        EXPECT_FALSE(static_cast<bool>(result)) << name;
        EXPECT_NE(result.error.find(message), std::string::npos) << name << ": " << result.error;
    };

    Json model = MinimalModel();
    model["meshes"][0]["primitives"][0]["attributes"].erase("POSITION");
    expectError("missing-position", model, "missing POSITION");

    model = MinimalModel();
    model["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"] = 0;
    expectError("skinning", model, "requires skinning support");

    model = MinimalModel();
    model["meshes"][0]["primitives"][0]["indices"] = 9;
    expectError("bad-index-accessor", model, "indices accessor out of bounds");

    model = MinimalModel();
    model["nodes"][0]["children"] = Json::array({0});
    expectError("self-cycle", model, "invalid or multiply-parented child");

    model = MinimalModel();
    model["nodes"][0]["mesh"] = 9;
    expectError("invalid-mesh", model, "invalid mesh");
}

TEST(AssetImporterTests, ImportsMaterialExtensionsTextureTransformsAndDecodedImages)
{
    ImportWorkspace workspace;
    WritePng(workspace.Path() / "color.png");
    Json model = MinimalModel();
    model["extensionsUsed"] = Json::array({"KHR_texture_transform", "KHR_materials_pbrSpecularGlossiness"});
    model["images"] = Json::array({{{"uri", "color.png"}, {"mimeType", "image/png"}}});
    model["samplers"] =
        Json::array({{{"magFilter", 9729}, {"minFilter", 9987}, {"wrapS", 33071}, {"wrapT", 33648}}});
    model["textures"] = Json::array({{{"source", 0}, {"sampler", 0}}});
    Json material;
    material["name"] = "gloss";
    material["alphaMode"] = "BLEND";
    material["pbrMetallicRoughness"]["baseColorTexture"]["index"] = 0;
    material["pbrMetallicRoughness"]["baseColorTexture"]["extensions"]["KHR_texture_transform"] = {
        {"offset", Json::array({0.25, 0.5})}, {"scale", Json::array({2.0, 3.0})}, {"rotation", 0.25}};
    material["extensions"]["KHR_materials_pbrSpecularGlossiness"] = {
        {"diffuseFactor", Json::array({0.8, 0.7, 0.6, 1.0})},
        {"specularFactor", Json::array({0.2, 0.3, 0.4})},
        {"glossinessFactor", 0.65},
        {"diffuseTexture", {{"index", 0}, {"texCoord", 0}}},
        {"specularGlossinessTexture", {{"index", 0}}}};
    model["materials"] = Json::array({material});
    model["meshes"][0]["primitives"][0]["material"] = 0;

    const auto path = workspace.Path() / "material-extensions.gltf";
    WriteModel(path, model);
    Engine::AssetImporter importer;
    const auto result = importer.ImportGltf(path);
    ASSERT_TRUE(static_cast<bool>(result)) << result.error;
    ASSERT_EQ(result.model.images.size(), 1U);
    EXPECT_EQ(result.model.images[0].width, 1U);
    EXPECT_EQ(result.model.images[0].rgba8.size(), 4U);
    ASSERT_EQ(result.model.materials.size(), 1U);
    const auto &importedMaterial = result.model.materials[0];
    EXPECT_TRUE(importedMaterial.specularGlossiness);
    EXPECT_EQ(importedMaterial.alphaMode, "BLEND");
    EXPECT_FLOAT_EQ(importedMaterial.glossiness, 0.65f);
    EXPECT_FLOAT_EQ(importedMaterial.baseColorTransform.offset.x, 0.25f);
    EXPECT_FLOAT_EQ(importedMaterial.baseColorTransform.scale.y, 3.0f);
    EXPECT_FLOAT_EQ(importedMaterial.baseColorTransform.rotation, 0.25f);
    EXPECT_EQ(result.model.textures[0].wrapS, 33071);
}

TEST(AssetImporterTests, ImportsBinaryGlbAndRejectsTruncatedContainer)
{
    ImportWorkspace workspace;
    Engine::AssetImporter importer;
    const auto binaryPath = workspace.Path() / "triangle.glb";
    WriteGlb(binaryPath);

    const auto imported = importer.ImportGltf(binaryPath);
    ASSERT_TRUE(static_cast<bool>(imported)) << imported.error;
    ASSERT_EQ(imported.model.meshes.size(), 1U);
    ASSERT_EQ(imported.model.meshes.front().primitives.size(), 1U);
    EXPECT_EQ(imported.model.meshes.front().primitives.front().indices, std::vector<uint32_t>({0, 1, 2}));

    const auto malformedPath = workspace.Path() / "truncated.glb";
    {
        std::ofstream output(malformedPath, std::ios::binary | std::ios::trunc);
        const std::array<uint32_t, 3> shortHeader = {0x46546c67, 2, 12};
        output.write(reinterpret_cast<const char *>(shortHeader.data()), sizeof(shortHeader));
        ASSERT_TRUE(output.good());
    }
    const auto malformed = importer.ImportGltf(malformedPath);
    EXPECT_FALSE(static_cast<bool>(malformed));
    EXPECT_NE(malformed.error.find("truncated.glb"), std::string::npos);
}

TEST(AssetImporterTests, RejectsMalformedMaterialExtensionValues)
{
    ImportWorkspace workspace;
    WritePng(workspace.Path() / "color.png");
    Engine::AssetImporter importer;
    const auto expectError = [&](const std::string &name, Json material, const std::string &expected) {
        Json model = MinimalModel();
        model["extensionsUsed"] =
            Json::array({"KHR_texture_transform", "KHR_materials_pbrSpecularGlossiness"});
        model["images"] = Json::array({{{"uri", "color.png"}, {"mimeType", "image/png"}}});
        model["textures"] = Json::array({{{"source", 0}}});
        model["materials"] = Json::array({std::move(material)});
        model["meshes"][0]["primitives"][0]["material"] = 0;
        const auto path = workspace.Path() / (name + ".gltf");
        WriteModel(path, model);
        const auto imported = importer.ImportGltf(path);
        EXPECT_FALSE(static_cast<bool>(imported)) << name;
        EXPECT_NE(imported.error.find(expected), std::string::npos) << name << ": " << imported.error;
    };

    Json material;
    material["pbrMetallicRoughness"]["baseColorTexture"]["index"] = 0;
    material["pbrMetallicRoughness"]["baseColorTexture"]["extensions"]["KHR_texture_transform"] = {
        {"offset", Json::array({0.25})}};
    expectError("bad-texture-transform-pair", material, "malformed KHR_texture_transform offset or scale");

    material = Json::object();
    material["pbrMetallicRoughness"]["baseColorTexture"]["index"] = 0;
    material["pbrMetallicRoughness"]["baseColorTexture"]["extensions"]["KHR_texture_transform"] = {
        {"rotation", "not-a-number"}};
    expectError("bad-texture-transform-rotation", material, "malformed KHR_texture_transform rotation");

    material = Json::object();
    material["pbrMetallicRoughness"]["baseColorTexture"]["index"] = 0;
    material["pbrMetallicRoughness"]["baseColorTexture"]["extensions"]["KHR_texture_transform"] = {
        {"texCoord", 8}};
    expectError("unsupported-texture-transform-coordinate", material, "unsupported texture coordinate set");

    material = Json::object();
    material["extensions"]["KHR_materials_pbrSpecularGlossiness"] = {
        {"diffuseFactor", Json::array({0.8, 0.7, 0.6})}};
    expectError("bad-specular-glossiness-factor", material, "malformed diffuseFactor");

    material = Json::object();
    material["extensions"]["KHR_materials_pbrSpecularGlossiness"] = Json::object({{"diffuseTexture", 0}});
    expectError("bad-extension-texture-info-type", material, "malformed diffuseTexture");

    material = Json::object();
    material["extensions"]["KHR_materials_pbrSpecularGlossiness"] = {{"diffuseTexture", {{"index", "0"}}}};
    expectError("bad-extension-texture-index-type", material, "malformed diffuseTexture");

    material = Json::object();
    material["extensions"]["KHR_materials_pbrSpecularGlossiness"] = {{"diffuseTexture", {{"index", -1}}}};
    expectError("negative-extension-texture-index", material, "malformed diffuseTexture");

    material = Json::object();
    material["extensions"]["KHR_materials_pbrSpecularGlossiness"] = {
        {"diffuseTexture", {{"index", 0}, {"texCoord", 8}}}};
    expectError("unsupported-extension-texture-coordinate", material, "unsupported texture coordinate set");

    material = Json::object();
    material["extensions"]["KHR_materials_pbrSpecularGlossiness"] = {
        {"diffuseTexture",
         {{"index", 0}, {"extensions", {{"KHR_texture_transform", {{"offset", Json::array({0.0})}}}}}}}};
    expectError("bad-extension-texture-transform", material,
                "malformed KHR_texture_transform offset or scale");

    material = Json::object();
    material["extensions"]["KHR_materials_pbrSpecularGlossiness"] = {{"glossinessFactor", 1.5}};
    expectError("out-of-range-glossiness", material, "out-of-range glossinessFactor");
}

TEST(AssetImporterTests, ImportsNodeTransformsAndRejectsInvalidSceneHierarchyData)
{
    ImportWorkspace workspace;
    Engine::AssetImporter importer;
    Json model = MinimalModel();
    model["nodes"][0]["translation"] = Json::array({1.0, 2.0, 3.0});
    model["nodes"][0]["rotation"] = Json::array({0.0, 0.0, 0.0, 1.0});
    model["nodes"][0]["scale"] = Json::array({2.0, 3.0, 4.0});
    const auto trsPath = workspace.Path() / "trs.gltf";
    WriteModel(trsPath, model);
    const auto trs = importer.ImportGltf(trsPath);
    ASSERT_TRUE(static_cast<bool>(trs)) << trs.error;
    ASSERT_EQ(trs.model.nodes.size(), 1U);
    EXPECT_FLOAT_EQ(trs.model.nodes[0].translation.y, 2.0f);
    EXPECT_FLOAT_EQ(trs.model.nodes[0].rotation.w, 1.0f);
    EXPECT_FLOAT_EQ(trs.model.nodes[0].scale.z, 4.0f);

    model = MinimalModel();
    model["nodes"][0]["matrix"] = Json::array({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 4, 5, 6, 1});
    const auto matrixPath = workspace.Path() / "matrix.gltf";
    WriteModel(matrixPath, model);
    const auto matrix = importer.ImportGltf(matrixPath);
    ASSERT_TRUE(static_cast<bool>(matrix)) << matrix.error;
    ASSERT_EQ(matrix.model.nodes.size(), 1U);
    EXPECT_TRUE(matrix.model.nodes[0].hasMatrix);
    EXPECT_FLOAT_EQ(matrix.model.nodes[0].matrix._41, 4.0f);

    const auto expectError = [&](const std::string &name, const Json &invalid, const std::string &message) {
        const auto path = workspace.Path() / (name + ".gltf");
        WriteModel(path, invalid);
        const auto result = importer.ImportGltf(path);
        EXPECT_FALSE(static_cast<bool>(result)) << name;
        EXPECT_NE(result.error.find(message), std::string::npos) << name << ": " << result.error;
    };
    model = MinimalModel();
    model["nodes"][0]["translation"] = Json::array({1, 2});
    expectError("malformed-translation", model, "malformed or conflicting transform fields");

    model = MinimalModel();
    model["nodes"][0]["matrix"] = Json::array({1, 0, 0});
    expectError("malformed-matrix", model, "malformed or conflicting transform fields");

    model = MinimalModel();
    model["scene"] = 9;
    expectError("invalid-default-scene", model, "default scene index is invalid");

    model = MinimalModel();
    model["scenes"][0]["nodes"] = Json::array({9});
    expectError("invalid-scene-root", model, "scene references an invalid node");

    model = MinimalModel();
    model["nodes"].push_back({{"name", "child"}});
    model["nodes"][0]["children"] = Json::array({1});
    model["scenes"][0]["nodes"] = Json::array({0, 1});
    expectError("child-is-also-scene-root", model, "scene root is also a child node");

    model = MinimalModel();
    model["nodes"].push_back({{"name", "child"}});
    model["nodes"][0]["children"] = Json::array({1});
    model["nodes"][1]["children"] = Json::array({0});
    expectError("multi-node-cycle", model, "node hierarchy contains a cycle");
}

TEST(AssetImporterTests, ReadsVertexAttributesAndRejectsInvalidAccessorLayouts)
{
    ImportWorkspace workspace;
    Engine::AssetImporter importer;
    Json model = MinimalModel();
    model["accessors"].push_back(
        {{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC2"}});
    model["meshes"][0]["primitives"][0]["attributes"]["NORMAL"] = 0;
    model["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"] = 2;
    const auto path = workspace.Path() / "vertex-attributes.gltf";
    WriteModel(path, model);
    const auto imported = importer.ImportGltf(path);
    ASSERT_TRUE(static_cast<bool>(imported)) << imported.error;
    const auto &vertices = imported.model.meshes[0].primitives[0].vertices;
    ASSERT_EQ(vertices.size(), 3U);
    EXPECT_FLOAT_EQ(vertices[1].UV[0].y, 1.0f);

    const auto expectError = [&](const std::string &name, const Json &invalid, const std::string &message) {
        const auto invalidPath = workspace.Path() / (name + ".gltf");
        WriteModel(invalidPath, invalid);
        const auto result = importer.ImportGltf(invalidPath);
        EXPECT_FALSE(static_cast<bool>(result)) << name;
        EXPECT_NE(result.error.find(message), std::string::npos) << name << ": " << result.error;
    };

    model = MinimalModel();
    model["meshes"][0]["primitives"][0]["attributes"]["NORMAL"] = 9;
    expectError("missing-normal-accessor", model, "NORMAL references an invalid accessor");

    model = MinimalModel();
    model["accessors"].push_back(
        {{"bufferView", 0}, {"componentType", 5123}, {"count", 3}, {"type", "VEC3"}});
    model["meshes"][0]["primitives"][0]["attributes"]["NORMAL"] = 2;
    expectError("invalid-normal-storage", model, "NORMAL uses an invalid component type");

    model = MinimalModel();
    model["accessors"][1]["componentType"] = 5126;
    model["accessors"][1]["count"] = 1;
    expectError("invalid-index-storage", model, "indices must use an unsigned scalar accessor");

    model = MinimalModel();
    model["accessors"][0]["bufferView"] = 9;
    expectError("invalid-buffer-view", model, "POSITION has no valid buffer view");

    model = MinimalModel();
    model["bufferViews"][0]["byteStride"] = 8;
    expectError("undersized-accessor-stride", model, "POSITION has an invalid byte stride");

    model = MinimalModel();
    model["accessors"][0]["sparse"] = {{"count", 1},
                                       {"indices", {{"bufferView", 1}, {"componentType", 5121}}},
                                       {"values", {{"bufferView", 0}}}};
    expectError("sparse-position-accessor", model, "POSITION uses a sparse accessor");

    model = MinimalModel();
    model["bufferViews"][0]["buffer"] = 7;
    expectError("invalid-accessor-buffer", model, "POSITION references an invalid buffer");

    model = MinimalModel();
    model["accessors"][0]["byteOffset"] = 40;
    expectError("accessor-offset-out-of-range", model, "POSITION has an out-of-range byte offset");

    model = MinimalModel();
    model["bufferViews"][0]["byteOffset"] = 36;
    expectError("buffer-view-offset-out-of-range", model, "POSITION extends beyond its source buffer");

    model = MinimalModel();
    model["accessors"][0]["count"] = 4;
    expectError("accessor-data-truncated", model, "POSITION accessor data is truncated");

    model = MinimalModel();
    model["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_invalid"] = 0;
    expectError("invalid-texture-coordinate-semantic", model, "has an invalid texture coordinate semantic");

    model = MinimalModel();
    model["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_"] = 0;
    expectError("empty-texture-coordinate-semantic", model, "has an invalid texture coordinate semantic");
}

TEST(AssetImporterTests, DecodesNormalizedSignedAndUnsignedVertexAttributes)
{
    ImportWorkspace workspace;
    Engine::AssetImporter importer;
    const std::array<float, 9> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::array<int8_t, 9> byteNormals = {0, 127, 0, 0, 127, 0, 0, 127, 0};
    const std::array<uint8_t, 6> byteUvs = {0, 255, 0, 255, 0, 255};
    const std::array<int16_t, 9> shortNormals = {0, 32767, 0, 0, 32767, 0, 0, 32767, 0};
    const std::array<uint16_t, 6> shortUvs = {0, 65535, 0, 65535, 0, 65535};
    const std::array<uint16_t, 3> indices = {0, 1, 2};

    const auto checkImport = [&](const std::string &name, int normalType, int uvType, const void *normalBytes,
                                 size_t normalSize, const void *uvBytes, size_t uvSize) {
        Json model = MinimalModel();
        model["buffers"][0]["byteLength"] = 36 + normalSize + uvSize + sizeof(indices);
        model["bufferViews"] = Json::array(
            {{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
             {{"buffer", 0}, {"byteOffset", 36 + normalSize + uvSize}, {"byteLength", sizeof(indices)}},
             {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", normalSize}},
             {{"buffer", 0}, {"byteOffset", 36 + normalSize}, {"byteLength", uvSize}}});
        model["accessors"][1]["bufferView"] = 1;
        model["accessors"].push_back({{"bufferView", 2},
                                      {"componentType", normalType},
                                      {"count", 3},
                                      {"type", "VEC3"},
                                      {"normalized", true}});
        model["accessors"].push_back({{"bufferView", 3},
                                      {"componentType", uvType},
                                      {"count", 3},
                                      {"type", "VEC2"},
                                      {"normalized", true}});
        model["meshes"][0]["primitives"][0]["attributes"]["NORMAL"] = 2;
        model["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"] = 3;

        std::vector<uint8_t> bytes(36 + normalSize + uvSize + sizeof(indices));
        size_t offset = 0;
        std::memcpy(bytes.data() + offset, positions.data(), sizeof(positions));
        offset += sizeof(positions);
        std::memcpy(bytes.data() + offset, normalBytes, normalSize);
        offset += normalSize;
        std::memcpy(bytes.data() + offset, uvBytes, uvSize);
        offset += uvSize;
        std::memcpy(bytes.data() + offset, indices.data(), sizeof(indices));

        const auto path = workspace.Path() / (name + ".gltf");
        WriteModelWithBinary(path, model, bytes);
        const auto result = importer.ImportGltf(path);
        ASSERT_TRUE(static_cast<bool>(result)) << result.error;
        const auto &vertices = result.model.meshes[0].primitives[0].vertices;
        ASSERT_EQ(vertices.size(), 3U);
        EXPECT_FLOAT_EQ(vertices[0].Normal.y, 1.0f);
        EXPECT_FLOAT_EQ(vertices[0].UV[0].y, 1.0f);
    };

    checkImport("normalized-byte-attributes", 5120, 5121, byteNormals.data(), sizeof(byteNormals),
                byteUvs.data(), sizeof(byteUvs));
    checkImport("normalized-short-attributes", 5122, 5123, shortNormals.data(), sizeof(shortNormals),
                shortUvs.data(), sizeof(shortUvs));
}

TEST(AssetImporterTests, ReportsUnsupportedAnimationDataAndRejectsInvalidTextureState)
{
    ImportWorkspace workspace;
    WritePng(workspace.Path() / "color.png");
    Engine::AssetImporter importer;
    Json model = MinimalModel();
    model["skins"] = Json::array({{{"joints", Json::array({0})}}});
    model["animations"] = Json::array({{{"samplers", Json::array()}, {"channels", Json::array()}}});
    const auto animationPath = workspace.Path() / "unsupported-animation-data.gltf";
    WriteModel(animationPath, model);
    const auto animation = importer.ImportGltf(animationPath);
    ASSERT_TRUE(static_cast<bool>(animation)) << animation.error;
    ASSERT_EQ(animation.warnings.size(), 2U);
    EXPECT_NE(animation.warnings[0].find("skinning is not imported"), std::string::npos);
    EXPECT_NE(animation.warnings[1].find("animations are not imported"), std::string::npos);

    const auto makeTexturedModel = [] {
        Json result = MinimalModel();
        result["images"] = Json::array({{{"uri", "color.png"}, {"mimeType", "image/png"}}});
        result["textures"] = Json::array({{{"source", 0}, {"sampler", 0}}});
        result["samplers"] =
            Json::array({{{"magFilter", 9729}, {"minFilter", 9987}, {"wrapS", 10497}, {"wrapT", 10497}}});
        Json material;
        material["pbrMetallicRoughness"]["baseColorTexture"]["index"] = 0;
        result["materials"] = Json::array({material});
        result["meshes"][0]["primitives"][0]["material"] = 0;
        return result;
    };
    const auto expectError = [&](const std::string &name, const Json &invalid, const std::string &message) {
        const auto path = workspace.Path() / (name + ".gltf");
        WriteModel(path, invalid);
        const auto result = importer.ImportGltf(path);
        EXPECT_FALSE(static_cast<bool>(result)) << name;
        EXPECT_NE(result.error.find(message), std::string::npos) << name << ": " << result.error;
    };

    model = makeTexturedModel();
    model["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["texCoord"] = 8;
    expectError("unsupported-texture-coordinate", model, "above TEXCOORD_7");

    model = makeTexturedModel();
    model["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["texCoord"] = 1;
    expectError("missing-texture-coordinate-attribute", model, "no matching attribute");

    model = makeTexturedModel();
    model["samplers"][0]["magFilter"] = 9986;
    expectError("invalid-sampler-filter", model, "invalid glTF sampler state");

    model = makeTexturedModel();
    model["textures"][0]["source"] = 9;
    expectError("invalid-texture-image", model, "texture references an invalid image");

    model = makeTexturedModel();
    model["textures"][0]["sampler"] = 9;
    expectError("invalid-texture-sampler", model, "texture references an invalid sampler");

    model = MinimalModel();
    model["meshes"][0]["primitives"][0]["material"] = 9;
    expectError("invalid-primitive-material", model, "primitive references an invalid material");

    model = makeTexturedModel();
    model["materials"][0]["alphaMode"] = "FADE";
    expectError("invalid-material-alpha-mode", model, "invalid alpha mode");
}
} // namespace
