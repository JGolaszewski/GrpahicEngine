#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 6262) // stb_image's decoder intentionally uses a large fixed stack frame.
#endif
#include <tiny_gltf.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "assets/include/AssetImporter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace Engine {
namespace {
auto Context(const std::filesystem::path& path, const std::string& message) -> std::string {
  return path.generic_string() + ": " + message;
}

auto IsSupportedImage(const tinygltf::Image& image) -> bool {
  std::string mimeType = image.mimeType;
  std::ranges::transform(mimeType, mimeType.begin(),
                         [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
  if (!mimeType.empty()) {
    return mimeType == "image/png" || mimeType == "image/jpeg" || mimeType == "image/jpg";
  }
  std::string extension = std::filesystem::path(image.uri).extension().string();
  std::ranges::transform(extension, extension.begin(),
                         [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
  return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

auto ReadTextureTransformValue(const tinygltf::Value& value, ImportedTextureTransform& transform, uint32_t& texCoord,
                               std::string& error, const std::string& label) -> bool {
  const auto readPair = [&](const char* key, DirectX::XMFLOAT2& destination) -> bool {
    if (!value.Has(key)) {
      return true;
    }
    const tinygltf::Value& pair = value.Get(key);
    if (!pair.IsArray() || pair.ArrayLen() != 2 || !pair.Get(0).IsNumber() || !pair.Get(1).IsNumber()) {
      return false;
    }
    destination = {static_cast<float>(pair.Get(0).GetNumberAsDouble()),
                   static_cast<float>(pair.Get(1).GetNumberAsDouble())};
    return std::isfinite(destination.x) && std::isfinite(destination.y);
  };
  if (!value.IsObject() || !readPair("offset", transform.offset) || !readPair("scale", transform.scale)) {
    error = label + " has a malformed KHR_texture_transform offset or scale";
    return false;
  }
  if (value.Has("rotation")) {
    const tinygltf::Value& rotation = value.Get("rotation");
    if (!rotation.IsNumber() || !std::isfinite(rotation.GetNumberAsDouble())) {
      error = label + " has a malformed KHR_texture_transform rotation";
      return false;
    }
    transform.rotation = static_cast<float>(rotation.GetNumberAsDouble());
  }
  if (value.Has("texCoord")) {
    const tinygltf::Value& coordinate = value.Get("texCoord");
    if (!coordinate.IsInt() || coordinate.GetNumberAsInt() < 0 ||
        coordinate.GetNumberAsInt() >= static_cast<int>(::Engine::Renderer::MaxTextureCoordinateSets)) {
      error = label + " selects an unsupported texture coordinate set";
      return false;
    }
    texCoord = static_cast<uint32_t>(coordinate.GetNumberAsInt());
  }
  return true;
}

auto ReadTextureTransform(const tinygltf::ExtensionMap& extensions, ImportedTextureTransform& transform,
                          uint32_t& texCoord, std::string& error, const std::string& label) -> bool {
  const auto extension = extensions.find("KHR_texture_transform");
  return extension == extensions.end() ||
         ReadTextureTransformValue(extension->second, transform, texCoord, error, label);
}

auto ReadExtensionFactor(const tinygltf::Value& value, const char* name, size_t componentCount, float* destination,
                         std::string& error, const std::string& label) -> bool {
  if (!value.Has(name)) {
    return true;
  }
  const auto& factor = value.Get(name);
  if (!factor.IsArray() || factor.ArrayLen() != componentCount) {
    error = label + " has a malformed " + name;
    return false;
  }
  for (size_t i = 0; i < componentCount; ++i) {
    const auto& component = factor.Get(i);
    if (!component.IsNumber()) {
      error = label + " has a malformed " + name;
      return false;
    }
    const double number = component.GetNumberAsDouble();
    if (!std::isfinite(number) || number < 0.0 || number > 1.0) {
      error = label + " has an out-of-range " + name;
      return false;
    }
    destination[i] = static_cast<float>(number);
  }
  return true;
}

auto ReadExtensionScalar(const tinygltf::Value& value, const char* name, float& destination, std::string& error,
                         const std::string& label) -> bool {
  if (!value.Has(name)) {
    return true;
  }
  const auto& factor = value.Get(name);
  if (!factor.IsNumber()) {
    error = label + " has a malformed " + name;
    return false;
  }
  const double number = factor.GetNumberAsDouble();
  if (!std::isfinite(number) || number < 0.0 || number > 1.0) {
    error = label + " has an out-of-range " + name;
    return false;
  }
  destination = static_cast<float>(number);
  return true;
}

auto ReadExtensionTextureInfo(const tinygltf::Value& materialExtension, const char* name, int32_t& textureIndex,
                              uint32_t& texCoord, ImportedTextureTransform& transform, std::string& error,
                              const std::string& label) -> bool {
  if (!materialExtension.Has(name)) {
    return true;
  }
  const auto& info = materialExtension.Get(name);
  if (!info.IsObject() || !info.Has("index") || !info.Get("index").IsInt() || info.Get("index").GetNumberAsInt() < 0) {
    error = label + " has a malformed " + name;
    return false;
  }
  textureIndex = info.Get("index").GetNumberAsInt();
  if (info.Has("texCoord")) {
    const auto& coordinate = info.Get("texCoord");
    if (!coordinate.IsInt() || coordinate.GetNumberAsInt() < 0 ||
        coordinate.GetNumberAsInt() >= static_cast<int>(::Engine::Renderer::MaxTextureCoordinateSets)) {
      error = label + " selects an unsupported texture coordinate set";
      return false;
    }
    texCoord = static_cast<uint32_t>(coordinate.GetNumberAsInt());
  }
  if (info.Has("extensions")) {
    const auto& extensions = info.Get("extensions");
    if (!extensions.IsObject()) {
      error = label + " has malformed texture extensions";
      return false;
    }
    if (extensions.Has("KHR_texture_transform") &&
        !ReadTextureTransformValue(extensions.Get("KHR_texture_transform"), transform, texCoord, error,
                                   label + "." + name)) {
      return false;
    }
  }
  return true;
}

auto GetAccessorData(const tinygltf::Model& model, int accessorIndex, const tinygltf::Accessor*& accessor,
                     const uint8_t*& data, size_t& stride, std::string& error, const std::string& label) -> bool {
  if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
    error = label + " references an invalid accessor";
    return false;
  }
  accessor = &model.accessors.at(accessorIndex);
  if (accessor->sparse.isSparse) {
    error = label + " uses a sparse accessor, which is not supported yet";
    return false;
  }
  if (accessor->bufferView < 0 || static_cast<size_t>(accessor->bufferView) >= model.bufferViews.size()) {
    error = label + " has no valid buffer view";
    return false;
  }
  const tinygltf::BufferView& view = model.bufferViews.at(accessor->bufferView);
  if (view.buffer < 0 || static_cast<size_t>(view.buffer) >= model.buffers.size()) {
    error = label + " references an invalid buffer";
    return false;
  }
  const std::vector<unsigned char>& bytes = model.buffers.at(view.buffer).data;
  const int componentSizeValue = tinygltf::GetComponentSizeInBytes(accessor->componentType);
  const int componentCount = tinygltf::GetNumComponentsInType(accessor->type);
  if (componentSizeValue <= 0 || componentCount <= 0) {
    error = label + " uses an invalid accessor type";
    return false;
  }
  const size_t componentSize = static_cast<size_t>(componentSizeValue);
  const size_t elementSize = componentSize * static_cast<size_t>(componentCount);
  if (view.byteStride != 0) {
    stride = view.byteStride;
    if (stride < elementSize || stride % componentSize != 0) {
      error = label + " has an invalid byte stride";
      return false;
    }
  } else {
    stride = elementSize;
  }
  if (accessor->byteOffset > view.byteLength || view.byteOffset > bytes.size()) {
    error = label + " has an out-of-range byte offset";
    return false;
  }
  const size_t start = view.byteOffset + accessor->byteOffset;
  if (start > bytes.size() || view.byteLength > bytes.size() - view.byteOffset) {
    error = label + " extends beyond its source buffer";
    return false;
  }
  if (accessor->count != 0) {
    if (accessor->count - 1 > (std::numeric_limits<size_t>::max() - elementSize) / stride) {
      error = label + " accessor size overflows";
      return false;
    }
    const size_t required = ((accessor->count - 1) * stride) + elementSize;
    if (required > view.byteLength - accessor->byteOffset || required > bytes.size() - start) {
      error = label + " accessor data is truncated";
      return false;
    }
  }
  data = bytes.data() + start;
  return true;
}

template <typename T> auto ReadComponent(const uint8_t* source) -> T {
  T value;
  std::memcpy(&value, source, sizeof(T));
  return value;
}

auto ReadAttributeComponent(const uint8_t* source, int componentType, bool normalized) -> double {
  switch (componentType) {
  case TINYGLTF_COMPONENT_TYPE_FLOAT:
    return ReadComponent<float>(source);
  case TINYGLTF_COMPONENT_TYPE_BYTE: {
    const auto value = ReadComponent<int8_t>(source);
    return normalized ? (std::max)(static_cast<double>(value) / 127.0, -1.0) : value;
  }
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
    const auto value = ReadComponent<uint8_t>(source);
    return normalized ? static_cast<double>(value) / 255.0 : value;
  }
  case TINYGLTF_COMPONENT_TYPE_SHORT: {
    const auto value = ReadComponent<int16_t>(source);
    return normalized ? (std::max)(static_cast<double>(value) / 32767.0, -1.0) : value;
  }
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
    const auto value = ReadComponent<uint16_t>(source);
    return normalized ? static_cast<double>(value) / 65535.0 : value;
  }
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    return ReadComponent<uint32_t>(source);
  default:
    return std::numeric_limits<double>::quiet_NaN();
  }
}

auto ReadFloatAttribute(const tinygltf::Model& model, int accessorIndex, int expectedComponents,
                        std::vector<std::array<float, 4>>& values, std::string& error, const std::string& label)
    -> bool {
  const tinygltf::Accessor* accessor = nullptr;
  const uint8_t* data = nullptr;
  size_t stride = 0;
  if (!GetAccessorData(model, accessorIndex, accessor, data, stride, error, label)) {
    return false;
  }
  if (tinygltf::GetNumComponentsInType(accessor->type) != expectedComponents) {
    error = label + " has an unexpected vector size";
    return false;
  }
  const int componentType = accessor->componentType;
  const bool floatingPoint = componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && !accessor->normalized;
  const bool normalizedSigned = accessor->normalized && (componentType == TINYGLTF_COMPONENT_TYPE_BYTE ||
                                                         componentType == TINYGLTF_COMPONENT_TYPE_SHORT);
  const bool normalizedUnsigned = accessor->normalized && (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                                                           componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
  bool validStorage = floatingPoint || normalizedUnsigned;
  if (label == "NORMAL") {
    validStorage = floatingPoint || normalizedSigned;
  } else if (label == "POSITION" || label == "TANGENT") {
    validStorage = floatingPoint;
  }
  if (!validStorage) {
    error = label + " uses an invalid component type or normalization mode";
    return false;
  }
  const size_t componentSize = tinygltf::GetComponentSizeInBytes(accessor->componentType);
  values.resize(accessor->count);
  for (size_t i = 0; i < accessor->count; ++i) {
    for (int c = 0; c < expectedComponents; ++c) {
      const double value = ReadAttributeComponent(data + (i * stride) + (static_cast<size_t>(c) * componentSize),
                                                  accessor->componentType, accessor->normalized);
      if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
        error = label + " contains a non-finite or unrepresentable value";
        return false;
      }
      values.at(i).at(c) = static_cast<float>(value);
    }
  }
  return true;
}

auto ReadIndices(const tinygltf::Model& model, int accessorIndex, size_t vertexCount, std::vector<uint32_t>& indices,
                 std::string& error) -> bool {
  const tinygltf::Accessor* accessor = nullptr;
  const uint8_t* data = nullptr;
  size_t stride = 0;
  if (!GetAccessorData(model, accessorIndex, accessor, data, stride, error, "indices")) {
    return false;
  }
  if (accessor->type != TINYGLTF_TYPE_SCALAR || accessor->normalized ||
      (accessor->componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
       accessor->componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
       accessor->componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)) {
    error = "indices must use an unsigned scalar accessor";
    return false;
  }
  indices.resize(accessor->count);
  for (size_t i = 0; i < accessor->count; ++i) {
    const uint8_t* item = data + (i * stride);
    uint32_t index = 0;
    switch (accessor->componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
      index = ReadComponent<uint8_t>(item);
      break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
      index = ReadComponent<uint16_t>(item);
      break;
    default:
      index = ReadComponent<uint32_t>(item);
      break;
    }
    if (index >= vertexCount) {
      error = "indices contains a vertex index outside the POSITION accessor";
      return false;
    }
    indices.at(i) = index;
  }
  return true;
}

auto Normalize(const DirectX::XMFLOAT3& v, const DirectX::XMFLOAT3& fallback) -> DirectX::XMFLOAT3 {
  const float lengthSquared = (v.x * v.x) + (v.y * v.y) + (v.z * v.z);
  if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-20F) {
    return fallback;
  }
  const float inverseLength = 1.0F / std::sqrt(lengthSquared);
  return {v.x * inverseLength, v.y * inverseLength, v.z * inverseLength};
}

void GenerateNormals(ImportedPrimitive& primitive) {
  std::vector<DirectX::XMFLOAT3> accumulated(primitive.vertices.size());
  for (size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
    auto& a = primitive.vertices.at(primitive.indices.at(i)).Position;
    auto& b = primitive.vertices.at(primitive.indices.at(i + 1)).Position;
    auto& c = primitive.vertices.at(primitive.indices.at(i + 2)).Position;
    const DirectX::XMFLOAT3 edge1{b.x - a.x, b.y - a.y, b.z - a.z};
    const DirectX::XMFLOAT3 edge2{c.x - a.x, c.y - a.y, c.z - a.z};
    const DirectX::XMFLOAT3 normal{(edge1.y * edge2.z) - (edge1.z * edge2.y), (edge1.z * edge2.x) - (edge1.x * edge2.z),
                                   (edge1.x * edge2.y) - (edge1.y * edge2.x)};
    for (size_t corner = 0; corner < 3; ++corner) {
      auto& sum = accumulated.at(primitive.indices.at(i + corner));
      sum.x += normal.x;
      sum.y += normal.y;
      sum.z += normal.z;
    }
  }
  for (size_t i = 0; i < primitive.vertices.size(); ++i) {
    primitive.vertices.at(i).Normal = Normalize(accumulated.at(i), {0.0F, 1.0F, 0.0F});
  }
}

void GenerateTangents(ImportedPrimitive& primitive, uint32_t texCoord) {
  std::vector<DirectX::XMFLOAT3> tangent(primitive.vertices.size());
  std::vector<DirectX::XMFLOAT3> bitangent(primitive.vertices.size());
  for (size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
    const uint32_t ia = primitive.indices.at(i);
    const uint32_t ib = primitive.indices.at(i + 1);
    const uint32_t ic = primitive.indices.at(i + 2);
    const auto& a = primitive.vertices.at(ia);
    const auto& b = primitive.vertices.at(ib);
    const auto& c = primitive.vertices.at(ic);
    const float x1 = b.Position.x - a.Position.x;
    const float x2 = c.Position.x - a.Position.x;
    const float y1 = b.Position.y - a.Position.y;
    const float y2 = c.Position.y - a.Position.y;
    const float z1 = b.Position.z - a.Position.z;
    const float z2 = c.Position.z - a.Position.z;
    const float s1 = b.UV[texCoord].x - a.UV[texCoord].x;
    const float s2 = c.UV[texCoord].x - a.UV[texCoord].x;
    const float t1 = b.UV[texCoord].y - a.UV[texCoord].y;
    const float t2 = c.UV[texCoord].y - a.UV[texCoord].y;
    const float determinant = (s1 * t2) - (s2 * t1);
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-20F) {
      continue;
    }
    const float r = 1.0F / determinant;
    const DirectX::XMFLOAT3 sdir{((t2 * x1) - (t1 * x2)) * r, ((t2 * y1) - (t1 * y2)) * r, ((t2 * z1) - (t1 * z2)) * r};
    const DirectX::XMFLOAT3 tdir{((s1 * x2) - (s2 * x1)) * r, ((s1 * y2) - (s2 * y1)) * r, ((s1 * z2) - (s2 * z1)) * r};
    for (uint32_t index : {ia, ib, ic}) {
      tangent.at(index).x += sdir.x;
      tangent.at(index).y += sdir.y;
      tangent.at(index).z += sdir.z;
      bitangent.at(index).x += tdir.x;
      bitangent.at(index).y += tdir.y;
      bitangent.at(index).z += tdir.z;
    }
  }
  for (size_t i = 0; i < primitive.vertices.size(); ++i) {
    const auto& normal = primitive.vertices.at(i).Normal;
    const auto& source = tangent.at(i);
    const float projection = (normal.x * source.x) + (normal.y * source.y) + (normal.z * source.z);
    const DirectX::XMFLOAT3 orthogonal{source.x - (normal.x * projection), source.y - (normal.y * projection),
                                       source.z - (normal.z * projection)};
    const DirectX::XMFLOAT3 fallback = std::abs(normal.y) < 0.9F
                                           ? Normalize({normal.z, 0.0F, -normal.x}, {1.0F, 0.0F, 0.0F})
                                           : DirectX::XMFLOAT3{1.0F, 0.0F, 0.0F};
    const auto direction = Normalize(orthogonal, fallback);
    const auto& bitan = bitangent.at(i);
    const float handedness = ((normal.x * ((direction.y * bitan.z) - (direction.z * bitan.y))) +
                              (normal.y * ((direction.z * bitan.x) - (direction.x * bitan.z))) +
                              (normal.z * ((direction.x * bitan.y) - (direction.y * bitan.x)))) < 0.0F
                                 ? -1.0F
                                 : 1.0F;
    primitive.vertices.at(i).Tangent = {direction.x, direction.y, direction.z, handedness};
  }
}

auto ValidateTextureCoordinateSemantic(const std::string& semantic, std::string& error) -> bool {
  if (!semantic.starts_with("TEXCOORD_")) {
    return true;
  }
  if (semantic.find_first_not_of("0123456789", sizeof("TEXCOORD_") - 1) != std::string::npos ||
      semantic.size() == sizeof("TEXCOORD_") - 1) {
    error = semantic + " has an invalid texture coordinate semantic";
    return false;
  }
  uint32_t coordinate = 0;
  const char* begin = semantic.data() + sizeof("TEXCOORD_") - 1;
  const char* end = semantic.data() + semantic.size();
  const auto [parsedEnd, parseError] = std::from_chars(begin, end, coordinate);
  if (parseError != std::errc{} || parsedEnd != end || semantic != "TEXCOORD_" + std::to_string(coordinate)) {
    error = semantic + " has an invalid texture coordinate semantic";
    return false;
  }
  if (coordinate >= ::Engine::Renderer::MaxTextureCoordinateSets) {
    error = semantic + " is not supported; texture coordinates are limited to TEXCOORD_7";
    return false;
  }
  return true;
}

auto ValidatePrimitiveAttributeSemantic(const std::string& semantic, std::string& error) -> bool {
  if (semantic.starts_with("JOINTS_") || semantic.starts_with("WEIGHTS_")) {
    error = semantic + " requires skinning support, which is not implemented";
    return false;
  }
  return ValidateTextureCoordinateSemantic(semantic, error);
}

auto ValidatePrimitiveAttributeCounts(
    const std::vector<std::array<float, 4>>& positions, const std::vector<std::array<float, 4>>& normals,
    const std::vector<std::array<float, 4>>& tangents, const std::vector<std::array<float, 4>>& colors,
    const std::array<std::vector<std::array<float, 4>>, ::Engine::Renderer::MaxTextureCoordinateSets>& uvs,
    std::string& error) -> bool {
  if ((!normals.empty() && normals.size() != positions.size()) ||
      (!colors.empty() && colors.size() != positions.size()) ||
      (!tangents.empty() && tangents.size() != positions.size())) {
    error = "vertex attribute counts do not match POSITION";
    return false;
  }
  if (std::ranges::any_of(
          uvs, [&positions](const auto& uv) -> bool { return !uv.empty() && uv.size() != positions.size(); })) {
    error = "vertex attribute counts do not match POSITION";
    return false;
  }
  return true;
}

auto ReadPrimitiveAttributes(
    const tinygltf::Model& source, const tinygltf::Primitive& input, std::vector<std::array<float, 4>>& positions,
    std::vector<std::array<float, 4>>& normals, std::vector<std::array<float, 4>>& tangents,
    std::vector<std::array<float, 4>>& colors,
    std::array<std::vector<std::array<float, 4>>, ::Engine::Renderer::MaxTextureCoordinateSets>& uvs,
    std::string& error) -> bool {
  const auto position = input.attributes.find("POSITION");
  if (position == input.attributes.end()) {
    error = "primitive is missing POSITION";
    return false;
  }
  if (!ReadFloatAttribute(source, position->second, 3, positions, error, "POSITION")) {
    return false;
  }
  if (positions.empty() || positions.size() > std::numeric_limits<uint32_t>::max()) {
    error = "POSITION has an invalid vertex count";
    return false;
  }
  const auto readOptional = [&](const char* name, int components, std::vector<std::array<float, 4>>& values) -> bool {
    const auto attribute = input.attributes.find(name);
    return attribute == input.attributes.end() ||
           ReadFloatAttribute(source, attribute->second, components, values, error, name);
  };
  if (!readOptional("NORMAL", 3, normals) || !readOptional("TANGENT", 4, tangents)) {
    return false;
  }
  for (uint32_t uvIndex = 0; uvIndex < ::Engine::Renderer::MaxTextureCoordinateSets; ++uvIndex) {
    const std::string semantic = "TEXCOORD_" + std::to_string(uvIndex);
    if (!readOptional(semantic.c_str(), 2, uvs.at(uvIndex))) {
      return false;
    }
  }
  if (const auto color = input.attributes.find("COLOR_0"); color != input.attributes.end()) {
    const int accessorIndex = color->second;
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= source.accessors.size()) {
      error = "COLOR_0 references an invalid accessor";
      return false;
    }
    const int components = tinygltf::GetNumComponentsInType(source.accessors.at(accessorIndex).type);
    if ((components != 3 && components != 4) ||
        !ReadFloatAttribute(source, accessorIndex, components, colors, error, "COLOR_0")) {
      if (error.empty()) {
        error = "COLOR_0 must use VEC3 or VEC4 values";
      }
      return false;
    }
  }
  for (const auto& [semantic, accessor] : input.attributes) {
    (void)accessor;
    if (!ValidatePrimitiveAttributeSemantic(semantic, error)) {
      return false;
    }
  }
  return ValidatePrimitiveAttributeCounts(positions, normals, tangents, colors, uvs, error);
}

auto BuildPrimitiveVertices(
    const tinygltf::Model& source, const tinygltf::Primitive& input, const std::vector<std::array<float, 4>>& positions,
    const std::vector<std::array<float, 4>>& normals, const std::vector<std::array<float, 4>>& tangents,
    const std::vector<std::array<float, 4>>& colors,
    const std::array<std::vector<std::array<float, 4>>, ::Engine::Renderer::MaxTextureCoordinateSets>& uvs,
    ImportedPrimitive& output) -> void {
  output.vertices.resize(positions.size());
  for (size_t vertexIndex = 0; vertexIndex < positions.size(); ++vertexIndex) {
    auto& vertex = output.vertices.at(vertexIndex);
    vertex.Position = {positions.at(vertexIndex).at(0), positions.at(vertexIndex).at(1),
                       positions.at(vertexIndex).at(2)};
    if (!normals.empty()) {
      vertex.Normal =
          Normalize({normals.at(vertexIndex).at(0), normals.at(vertexIndex).at(1), normals.at(vertexIndex).at(2)},
                    {0.0F, 1.0F, 0.0F});
    }
    for (size_t uvIndex = 0; uvIndex < uvs.size(); ++uvIndex) {
      if (!uvs.at(uvIndex).empty()) {
        vertex.UV[uvIndex] = {uvs.at(uvIndex).at(vertexIndex).at(0), uvs.at(uvIndex).at(vertexIndex).at(1)};
      }
    }
    if (!colors.empty()) {
      const auto& color = colors.at(vertexIndex);
      const auto colorType = source.accessors.at(input.attributes.at("COLOR_0")).type;
      vertex.Color = {color.at(0), color.at(1), color.at(2), colorType == TINYGLTF_TYPE_VEC4 ? color.at(3) : 1.0F};
    }
    if (!tangents.empty()) {
      const auto& tangent = tangents.at(vertexIndex);
      const auto direction = Normalize({tangent.at(0), tangent.at(1), tangent.at(2)}, {1.0F, 0.0F, 0.0F});
      vertex.Tangent = {direction.x, direction.y, direction.z, tangent.at(3) < 0.0F ? -1.0F : 1.0F};
    }
  }
}

auto ConvertPrimitiveTopology(int mode, ImportedPrimitive& output, std::string& error) -> bool {
  if (mode == TINYGLTF_MODE_LINE_LOOP) {
    if (output.indices.size() < 2) {
      error = "line loop requires at least two indices";
      return false;
    }
    const auto sourceIndices = std::move(output.indices);
    output.indices.clear();
    output.indices.reserve(sourceIndices.size() * 2);
    for (size_t index = 0; index < sourceIndices.size(); ++index) {
      output.indices.insert(output.indices.end(),
                            {sourceIndices.at(index), sourceIndices.at((index + 1) % sourceIndices.size())});
    }
  } else if (mode == TINYGLTF_MODE_TRIANGLE_STRIP || mode == TINYGLTF_MODE_TRIANGLE_FAN) {
    if (output.indices.size() < 3) {
      error = "triangle strip or fan requires at least three indices";
      return false;
    }
    const auto sourceIndices = std::move(output.indices);
    output.indices.clear();
    output.indices.reserve((sourceIndices.size() - 2) * 3);
    for (size_t index = 2; index < sourceIndices.size(); ++index) {
      if (mode == TINYGLTF_MODE_TRIANGLE_STRIP) {
        const uint32_t a = sourceIndices.at(index - 2);
        const uint32_t b = sourceIndices.at(index - 1);
        const uint32_t c = sourceIndices.at(index);
        if ((index & 1U) == 0U) {
          output.indices.insert(output.indices.end(), {a, b, c});
        } else {
          output.indices.insert(output.indices.end(), {b, a, c});
        }
      } else {
        output.indices.insert(output.indices.end(),
                              {sourceIndices.at(0), sourceIndices.at(index - 1), sourceIndices.at(index)});
      }
    }
  }
  return true;
}

void FinalizePrimitive(ImportedPrimitive& output, bool hasNormals, bool hasTangents, uint32_t tangentTexCoord) {
  output.boundsMin = output.boundsMax = output.vertices.front().Position;
  for (const auto& vertex : output.vertices) {
    const auto& position = vertex.Position;
    output.boundsMin.x = (std::min)(output.boundsMin.x, position.x);
    output.boundsMin.y = (std::min)(output.boundsMin.y, position.y);
    output.boundsMin.z = (std::min)(output.boundsMin.z, position.z);
    output.boundsMax.x = (std::max)(output.boundsMax.x, position.x);
    output.boundsMax.y = (std::max)(output.boundsMax.y, position.y);
    output.boundsMax.z = (std::max)(output.boundsMax.z, position.z);
  }
  if (!hasNormals && output.primitiveType == nvrhi::PrimitiveType::TriangleList) {
    GenerateNormals(output);
  }
  if (!hasTangents && output.primitiveType == nvrhi::PrimitiveType::TriangleList) {
    GenerateTangents(output, tangentTexCoord);
  }
}

auto ImportPrimitive(const tinygltf::Model& source, const tinygltf::Primitive& input, ImportedPrimitive& output,
                     std::string& error, uint32_t tangentTexCoord) -> bool {
  const int mode = input.mode < 0 ? TINYGLTF_MODE_TRIANGLES : input.mode;
  if (mode < TINYGLTF_MODE_POINTS || mode > TINYGLTF_MODE_TRIANGLE_FAN) {
    error = "primitive uses an unsupported topology";
    return false;
  }
  if (mode == TINYGLTF_MODE_LINE || mode == TINYGLTF_MODE_LINE_LOOP) {
    output.primitiveType = nvrhi::PrimitiveType::LineList;
  } else if (mode == TINYGLTF_MODE_LINE_STRIP) {
    output.primitiveType = nvrhi::PrimitiveType::LineStrip;
  } else if (mode == TINYGLTF_MODE_POINTS) {
    output.primitiveType = nvrhi::PrimitiveType::PointList;
  }
  std::vector<std::array<float, 4>> positions;
  std::vector<std::array<float, 4>> normals;
  std::vector<std::array<float, 4>> tangents;
  std::vector<std::array<float, 4>> colors;
  std::array<std::vector<std::array<float, 4>>, ::Engine::Renderer::MaxTextureCoordinateSets> uvs;
  if (!ReadPrimitiveAttributes(source, input, positions, normals, tangents, colors, uvs, error)) {
    return false;
  }
  BuildPrimitiveVertices(source, input, positions, normals, tangents, colors, uvs, output);
  if (input.indices < 0) {
    output.indices.resize(output.vertices.size());
    for (size_t index = 0; index < output.indices.size(); ++index) {
      output.indices.at(index) = static_cast<uint32_t>(index);
    }
  } else if (!ReadIndices(source, input.indices, output.vertices.size(), output.indices, error)) {
    return false;
  }
  if (!ConvertPrimitiveTopology(mode, output, error)) {
    return false;
  }
  const size_t elementCount = output.indices.size();
  if ((output.primitiveType == nvrhi::PrimitiveType::TriangleList && (elementCount < 3 || elementCount % 3 != 0)) ||
      (output.primitiveType == nvrhi::PrimitiveType::LineList && (elementCount < 2 || elementCount % 2 != 0)) ||
      (output.primitiveType == nvrhi::PrimitiveType::LineStrip && elementCount < 2) ||
      (output.primitiveType == nvrhi::PrimitiveType::PointList && elementCount == 0)) {
    error = "primitive has too few elements or an invalid element count for its topology";
    return false;
  }
  output.materialIndex = input.material;
  FinalizePrimitive(output, !normals.empty(), !tangents.empty(), tangentTexCoord);
  return true;
}

auto ValidTextureIndex(int index, size_t size) -> bool;

auto ImportGltfTextures(const tinygltf::Model& source, const std::filesystem::path& sourcePath, ImportedModel& model,
                        std::string& error) -> bool {
  model.textures.reserve(source.textures.size());
  for (const auto& input : source.textures) {
    if (input.source < 0 || static_cast<size_t>(input.source) >= source.images.size()) {
      error = Context(sourcePath, "texture references an invalid image");
      return false;
    }
    if (input.sampler >= 0 && static_cast<size_t>(input.sampler) >= source.samplers.size()) {
      error = Context(sourcePath, "texture references an invalid sampler");
      return false;
    }
    ImportedTexture texture;
    texture.imageIndex = input.source;
    texture.samplerIndex = input.sampler;
    if (input.sampler >= 0) {
      const auto& sampler = source.samplers.at(input.sampler);
      texture.magFilter = sampler.magFilter;
      texture.minFilter = sampler.minFilter;
      texture.wrapS = sampler.wrapS;
      texture.wrapT = sampler.wrapT;
    }
    const auto validMinFilter = [](int filter) -> bool {
      return filter < 0 || filter == 9728 || filter == 9729 || (filter >= 9984 && filter <= 9987);
    };
    const auto validMagFilter = [](int filter) -> bool { return filter < 0 || filter == 9728 || filter == 9729; };
    const auto validAddress = [](int address) -> bool {
      return address == 33071 || address == 33648 || address == 10497;
    };
    if (!validMagFilter(texture.magFilter) || !validMinFilter(texture.minFilter) || texture.wrapS < 0 ||
        texture.wrapT < 0 || !validAddress(texture.wrapS) || !validAddress(texture.wrapT)) {
      error = Context(sourcePath, "texture uses an invalid glTF sampler state");
      return false;
    }
    model.textures.push_back(texture);
  }
  for (const auto& material : model.materials) {
    for (int texture : {
             material.baseColorTexture,
             material.metallicRoughnessTexture,
             material.normalTexture,
             material.occlusionTexture,
             material.emissiveTexture,
         }) {
      if (!ValidTextureIndex(texture, model.textures.size())) {
        error = Context(sourcePath, "material references an invalid texture");
        return false;
      }
      if (texture < 0) {
        continue;
      }
    }
  }

  return true;
}

auto ImportGltfImages(const tinygltf::Model& source, const std::filesystem::path& sourcePath, ImportedModel& model,
                      std::string& error) -> bool {
  model.images.reserve(source.images.size());
  for (const auto& input : source.images) {
    if (!IsSupportedImage(input)) {
      error =
          Context(sourcePath, "image format is unsupported: " + (input.mimeType.empty() ? input.uri : input.mimeType));
      return false;
    }
    if (input.width <= 0 || input.height <= 0 || input.component <= 0 || input.bits != 8 ||
        input.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
      error = Context(sourcePath, "image decoder returned an unsupported pixel layout");
      return false;
    }
    const size_t width = static_cast<size_t>(input.width);
    const size_t height = static_cast<size_t>(input.height);
    if (width > std::numeric_limits<size_t>::max() / height ||
        width * height > std::numeric_limits<size_t>::max() / 4 || input.image.size() != width * height * 4) {
      error = Context(sourcePath, "image pixel data has an invalid size");
      return false;
    }
    ImportedImage image;
    image.name = input.name;
    image.mimeType = input.mimeType;
    image.width = static_cast<uint32_t>(input.width);
    image.height = static_cast<uint32_t>(input.height);
    image.rgba8.assign(input.image.begin(), input.image.end());
    model.images.push_back(std::move(image));
  }

  return true;
}

auto ImportGltfPrimitive(const tinygltf::Model& source, const tinygltf::Primitive& primitive,
                         const std::string& meshName, const std::filesystem::path& sourcePath,
                         const ImportedModel& model, std::string& error) -> std::optional<ImportedPrimitive> {
  if (primitive.material < -1 ||
      (primitive.material >= 0 && static_cast<size_t>(primitive.material) >= model.materials.size())) {
    error = Context(sourcePath, "primitive references an invalid material");
    return std::nullopt;
  }
  ImportedPrimitive imported;
  imported.name = meshName;
  const uint32_t tangentTexCoord =
      primitive.material >= 0 ? model.materials.at(static_cast<size_t>(primitive.material)).normalTexCoord : 0;
  if (!ImportPrimitive(source, primitive, imported, error, tangentTexCoord)) {
    error = Context(sourcePath, error);
    return std::nullopt;
  }
  if (primitive.material >= 0) {
    const auto& material = model.materials.at(static_cast<size_t>(primitive.material));
    const std::array<uint32_t, 5> coordinates = {
        material.baseColorTexCoord, material.metallicRoughnessTexCoord, material.normalTexCoord,
        material.occlusionTexCoord, material.emissiveTexCoord,
    };
    for (uint32_t coordinate : coordinates) {
      if (coordinate != 0 && !primitive.attributes.contains("TEXCOORD_" + std::to_string(coordinate))) {
        error = Context(sourcePath, "material selects TEXCOORD_" + std::to_string(coordinate) +
                                        " but its primitive has no matching attribute");
        return std::nullopt;
      }
    }
  }
  return imported;
}

auto ImportGltfMeshes(const tinygltf::Model& source, const std::filesystem::path& sourcePath, ImportedModel& model,
                      std::string& error) -> bool {
  model.meshes.reserve(source.meshes.size());
  for (const auto& input : source.meshes) {
    ImportedMesh mesh;
    mesh.name = input.name;
    mesh.primitives.reserve(input.primitives.size());
    for (const auto& primitive : input.primitives) {
      auto imported = ImportGltfPrimitive(source, primitive, mesh.name, sourcePath, model, error);
      if (!imported) {
        return false;
      }
      mesh.primitives.push_back(std::move(*imported));
    }
    model.meshes.push_back(std::move(mesh));
  }
  return true;
}

auto ImportMaterial(const tinygltf::Material& input, const std::filesystem::path& sourcePath, std::string& error)
    -> std::optional<ImportedMaterial> {
  ImportedMaterial material;
  material.name = input.name;
  if (input.pbrMetallicRoughness.baseColorFactor.size() == 4) {
    material.baseColor = {static_cast<float>(input.pbrMetallicRoughness.baseColorFactor.at(0)),
                          static_cast<float>(input.pbrMetallicRoughness.baseColorFactor.at(1)),
                          static_cast<float>(input.pbrMetallicRoughness.baseColorFactor.at(2)),
                          static_cast<float>(input.pbrMetallicRoughness.baseColorFactor.at(3))};
  }
  if (input.emissiveFactor.size() == 3) {
    material.emissive = {static_cast<float>(input.emissiveFactor.at(0)), static_cast<float>(input.emissiveFactor.at(1)),
                         static_cast<float>(input.emissiveFactor.at(2))};
  }
  material.metallic = static_cast<float>(input.pbrMetallicRoughness.metallicFactor);
  material.roughness = static_cast<float>(input.pbrMetallicRoughness.roughnessFactor);
  material.alphaMode = input.alphaMode;
  if (material.alphaMode != "OPAQUE" && material.alphaMode != "MASK" && material.alphaMode != "BLEND") {
    error = Context(sourcePath, "material contains an invalid alpha mode");
    return std::nullopt;
  }
  material.alphaCutoff = static_cast<float>(input.alphaCutoff);
  material.normalScale = static_cast<float>(input.normalTexture.scale);
  material.occlusionStrength = static_cast<float>(input.occlusionTexture.strength);
  material.doubleSided = input.doubleSided;
  material.baseColorTexture = input.pbrMetallicRoughness.baseColorTexture.index;
  material.metallicRoughnessTexture = input.pbrMetallicRoughness.metallicRoughnessTexture.index;
  material.normalTexture = input.normalTexture.index;
  material.occlusionTexture = input.occlusionTexture.index;
  material.emissiveTexture = input.emissiveTexture.index;
  material.baseColorTexCoord = input.pbrMetallicRoughness.baseColorTexture.texCoord;
  material.metallicRoughnessTexCoord = input.pbrMetallicRoughness.metallicRoughnessTexture.texCoord;
  material.normalTexCoord = input.normalTexture.texCoord;
  material.occlusionTexCoord = input.occlusionTexture.texCoord;
  material.emissiveTexCoord = input.emissiveTexture.texCoord;
  if (!ReadTextureTransform(input.pbrMetallicRoughness.baseColorTexture.extensions, material.baseColorTransform,
                            material.baseColorTexCoord, error, "baseColorTexture") ||
      !ReadTextureTransform(input.pbrMetallicRoughness.metallicRoughnessTexture.extensions,
                            material.metallicRoughnessTransform, material.metallicRoughnessTexCoord, error,
                            "metallicRoughnessTexture") ||
      !ReadTextureTransform(input.normalTexture.extensions, material.normalTransform, material.normalTexCoord, error,
                            "normalTexture") ||
      !ReadTextureTransform(input.occlusionTexture.extensions, material.occlusionTransform, material.occlusionTexCoord,
                            error, "occlusionTexture") ||
      !ReadTextureTransform(input.emissiveTexture.extensions, material.emissiveTransform, material.emissiveTexCoord,
                            error, "emissiveTexture")) {
    error = Context(sourcePath, error);
    return std::nullopt;
  }
  if (const auto extensionEntry = input.extensions.find("KHR_materials_pbrSpecularGlossiness");
      extensionEntry != input.extensions.end()) {
    const auto& value = extensionEntry->second;
    material.specularGlossiness = true;
    material.metallic = 0.0F;
    material.glossiness = 1.0F;
    std::array<float, 4> diffuseFactor = {1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 3> specularFactor = {1.0F, 1.0F, 1.0F};
    if (!value.IsObject() ||
        !ReadExtensionFactor(value, "diffuseFactor", 4, diffuseFactor.data(), error, "specular-glossiness") ||
        !ReadExtensionFactor(value, "specularFactor", 3, specularFactor.data(), error, "specular-glossiness") ||
        !ReadExtensionScalar(value, "glossinessFactor", material.glossiness, error, "specular-glossiness") ||
        !ReadExtensionTextureInfo(value, "diffuseTexture", material.baseColorTexture, material.baseColorTexCoord,
                                  material.baseColorTransform, error, "specular-glossiness") ||
        !ReadExtensionTextureInfo(value, "specularGlossinessTexture", material.metallicRoughnessTexture,
                                  material.metallicRoughnessTexCoord, material.metallicRoughnessTransform, error,
                                  "specular-glossiness")) {
      error = Context(sourcePath, error.empty() ? "malformed specular-glossiness extension" : error);
      return std::nullopt;
    }
    material.baseColor = {diffuseFactor.at(0), diffuseFactor.at(1), diffuseFactor.at(2), diffuseFactor.at(3)};
    material.specular = {specularFactor.at(0), specularFactor.at(1), specularFactor.at(2)};
  }
  if (material.baseColorTexCoord >= ::Engine::Renderer::MaxTextureCoordinateSets ||
      material.metallicRoughnessTexCoord >= ::Engine::Renderer::MaxTextureCoordinateSets ||
      material.normalTexCoord >= ::Engine::Renderer::MaxTextureCoordinateSets ||
      material.occlusionTexCoord >= ::Engine::Renderer::MaxTextureCoordinateSets ||
      material.emissiveTexCoord >= ::Engine::Renderer::MaxTextureCoordinateSets) {
    error = Context(sourcePath, "material selects a texture coordinate set above TEXCOORD_7");
    return std::nullopt;
  }
  return material;
}

auto ImportSceneRoots(const tinygltf::Model& source, const std::filesystem::path& sourcePath, ImportedModel& model,
                      std::string& error) -> bool {
  model.sourcePath = sourcePath;
  model.defaultScene = source.defaultScene;
  model.scenes.reserve(source.scenes.size());
  for (const auto& scene : source.scenes) {
    std::vector<uint32_t> roots;
    for (int node : scene.nodes) {
      if (node < 0 || static_cast<size_t>(node) >= source.nodes.size()) {
        error = Context(sourcePath, "scene references an invalid node");
        return false;
      }
      roots.push_back(static_cast<uint32_t>(node));
    }
    model.scenes.push_back(std::move(roots));
  }
  if (model.defaultScene < -1 ||
      (model.defaultScene >= 0 && static_cast<size_t>(model.defaultScene) >= model.scenes.size())) {
    error = Context(sourcePath, "default scene index is invalid");
    return false;
  }

  return true;
}

auto ReadNodeMatrix(const tinygltf::Node& input, ImportedNode& node, const std::filesystem::path& sourcePath,
                    std::string& error) -> bool {
  node.hasMatrix = true;
  // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) // DirectXMath exposes matrix elements through its documented
  // union layout.
  std::array<float*, 16> matrixMembers = {
      &node.matrix._11, &node.matrix._12, &node.matrix._13, &node.matrix._14, &node.matrix._21, &node.matrix._22,
      &node.matrix._23, &node.matrix._24, &node.matrix._31, &node.matrix._32, &node.matrix._33, &node.matrix._34,
      &node.matrix._41, &node.matrix._42, &node.matrix._43, &node.matrix._44,
  };
  // NOLINTEND(cppcoreguidelines-pro-type-union-access)
  for (size_t matrixIndex = 0; matrixIndex < matrixMembers.size(); ++matrixIndex) {
    const double value = input.matrix.at(matrixIndex);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
      error = Context(sourcePath, "node matrix contains a non-finite or unrepresentable value");
      return false;
    }
    *matrixMembers.at(matrixIndex) = static_cast<float>(value);
  }
  return true;
}

void ReadNodeComponents(const tinygltf::Node& input, ImportedNode& node) {
  if (!input.translation.empty()) {
    node.translation = {static_cast<float>(input.translation.at(0)), static_cast<float>(input.translation.at(1)),
                        static_cast<float>(input.translation.at(2))};
  }
  if (!input.rotation.empty()) {
    node.rotation = {static_cast<float>(input.rotation.at(0)), static_cast<float>(input.rotation.at(1)),
                     static_cast<float>(input.rotation.at(2)), static_cast<float>(input.rotation.at(3))};
  }
  if (!input.scale.empty()) {
    node.scale = {static_cast<float>(input.scale.at(0)), static_cast<float>(input.scale.at(1)),
                  static_cast<float>(input.scale.at(2))};
  }
}

auto ReadNodeTransform(const tinygltf::Node& input, const tinygltf::Model& source, ImportedNode& node,
                       const std::filesystem::path& sourcePath, std::string& error) -> bool {
  if ((!input.matrix.empty() && input.matrix.size() != 16) ||
      (!input.translation.empty() && input.translation.size() != 3) ||
      (!input.rotation.empty() && input.rotation.size() != 4) || (!input.scale.empty() && input.scale.size() != 3) ||
      (!input.matrix.empty() && (!input.translation.empty() || !input.rotation.empty() || !input.scale.empty()))) {
    error = Context(sourcePath, "node has malformed or conflicting transform fields");
    return false;
  }
  if (input.mesh >= 0 && static_cast<size_t>(input.mesh) >= source.meshes.size()) {
    error = Context(sourcePath, "node references an invalid mesh");
    return false;
  }
  node.meshIndex = input.mesh;
  if (input.matrix.size() == 16) {
    if (!ReadNodeMatrix(input, node, sourcePath, error)) {
      return false;
    }
  } else {
    ReadNodeComponents(input, node);
  }
  const std::array<float, 10> transformValues = {
      node.translation.x, node.translation.y, node.translation.z, node.rotation.x, node.rotation.y,
      node.rotation.z,    node.rotation.w,    node.scale.x,       node.scale.y,    node.scale.z,
  };
  if (!std::ranges::all_of(transformValues, [](float value) -> bool { return std::isfinite(value); })) {
    error = Context(sourcePath, "node transform contains a non-finite value");
    return false;
  }
  return true;
}

auto ImportNodeParents(const tinygltf::Model& source, ImportedModel& model, const std::filesystem::path& sourcePath,
                       std::string& error) -> bool {
  for (size_t nodeIndex = 0; nodeIndex < source.nodes.size(); ++nodeIndex) {
    const auto& input = source.nodes.at(nodeIndex);
    auto& node = model.nodes.at(nodeIndex);
    for (int child : input.children) {
      if (child < 0 || static_cast<size_t>(child) >= source.nodes.size() || std::cmp_equal(child, nodeIndex) ||
          model.nodes.at(child).parentIndex != -1) {
        error = Context(sourcePath, "node hierarchy contains an invalid or multiply-parented child");
        return false;
      }
      model.nodes.at(child).parentIndex = static_cast<int32_t>(nodeIndex);
      node.children.push_back(static_cast<uint32_t>(child));
    }
  }
  return true;
}

auto ImportNodeTransforms(const tinygltf::Model& source, const std::filesystem::path& sourcePath, ImportedModel& model,
                          std::string& error) -> bool {
  model.nodes.resize(source.nodes.size());
  for (size_t nodeIndex = 0; nodeIndex < source.nodes.size(); ++nodeIndex) {
    const auto& input = source.nodes.at(nodeIndex);
    auto& node = model.nodes.at(nodeIndex);
    node.name = input.name;
    if (!ReadNodeTransform(input, source, node, sourcePath, error)) {
      return false;
    }
  }
  return ImportNodeParents(source, model, sourcePath, error);
}

auto ImportNodeHierarchy(const std::filesystem::path& sourcePath, ImportedModel& model, std::string& error) -> bool {
  std::vector<uint8_t> visitState(model.nodes.size(), 0);
  const auto visitNode = [&](const auto& self, size_t index) -> bool {
    if (visitState.at(index) == 1) {
      return false;
    }
    if (visitState.at(index) == 2) {
      return true;
    }
    visitState.at(index) = 1;
    for (uint32_t child : model.nodes.at(index).children) {
      if (!self(self, child)) {
        return false;
      }
    }
    visitState.at(index) = 2;
    return true;
  };
  for (size_t i = 0; i < model.nodes.size(); ++i) {
    if (visitState.at(i) == 0 && !visitNode(visitNode, i)) {
      error = Context(sourcePath, "node hierarchy contains a cycle");
      return false;
    }
  }
  for (const auto& scene : model.scenes) {
    for (uint32_t root : scene) {
      if (model.nodes.at(root).parentIndex != -1) {
        error = Context(sourcePath, "scene root is also a child node");
        return false;
      }
    }
  }

  return true;
}

auto ValidTextureIndex(int index, size_t size) -> bool { return index < 0 || std::cmp_less(index, size); }
} // namespace

auto AssetImporter::ImportGltf(const std::filesystem::path& sourcePath) -> AssetImportResult {
  AssetImportResult result;
  const std::string extension = sourcePath.extension().string();
  std::string lowerExtension = extension;
  std::ranges::transform(lowerExtension, lowerExtension.begin(),
                         [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
  if (lowerExtension != ".gltf" && lowerExtension != ".glb") {
    result.error = Context(sourcePath, "expected a .gltf or .glb file");
    return result;
  }

  tinygltf::TinyGLTF loader;
  tinygltf::Model source;
  std::string warning;
  std::string error;
  const bool loaded = lowerExtension == ".glb"
                          ? loader.LoadBinaryFromFile(&source, &error, &warning, sourcePath.string())
                          : loader.LoadASCIIFromFile(&source, &error, &warning, sourcePath.string());
  if (!warning.empty()) {
    result.warnings.push_back(Context(sourcePath, warning));
  }
  if (!loaded) {
    result.error = Context(sourcePath, error.empty() ? "glTF parsing failed" : error);
    return result;
  }
  for (const auto& extensionName : source.extensionsRequired) {
    if (extensionName != "KHR_materials_pbrSpecularGlossiness" && extensionName != "KHR_texture_transform") {
      result.error = Context(sourcePath, "required glTF extension is unsupported: " + extensionName);
      return result;
    }
  }
  for (const auto& extensionName : source.extensionsUsed) {
    if (extensionName != "KHR_texture_transform" && extensionName != "KHR_materials_pbrSpecularGlossiness" &&
        std::ranges::find(source.extensionsRequired, extensionName) == source.extensionsRequired.end()) {
      result.warnings.push_back(Context(sourcePath, "optional glTF extension will be ignored: " + extensionName));
    }
  }
  if (!source.skins.empty()) {
    result.warnings.push_back(Context(sourcePath, "skinning is not imported; bind-pose geometry is used"));
  }
  if (!source.animations.empty()) {
    result.warnings.push_back(Context(sourcePath, "animations are not imported"));
  }

  if (!ImportSceneRoots(source, sourcePath, result.model, result.error) ||
      !ImportNodeTransforms(source, sourcePath, result.model, result.error) ||
      !ImportNodeHierarchy(sourcePath, result.model, result.error)) {
    return result;
  }

  result.model.materials.reserve(source.materials.size());
  for (const auto& input : source.materials) {
    auto material = ImportMaterial(input, sourcePath, error);
    if (!material) {
      result.error = error;
      return result;
    }
    result.model.materials.push_back(std::move(*material));
  }

  if (!ImportGltfTextures(source, sourcePath, result.model, result.error)) {
    return result;
  }

  if (!ImportGltfImages(source, sourcePath, result.model, result.error)) {
    return result;
  }

  if (!ImportGltfMeshes(source, sourcePath, result.model, result.error)) {
    return result;
  }

  return result;
}

auto AssetImporter::ImportImage(const std::filesystem::path& sourcePath) -> ImageImportResult {
  ImageImportResult result;
  std::string extension = sourcePath.extension().string();
  std::ranges::transform(extension, extension.begin(),
                         [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
  if (extension != ".png" && extension != ".jpg" && extension != ".jpeg") {
    result.error = Context(sourcePath, "expected a PNG or JPEG image");
    return result;
  }
  int width = 0;
  int height = 0;
  int channels = 0;
  const auto nativePath = sourcePath.string();
  stbi_uc* pixels = stbi_load(nativePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
  if (pixels == nullptr) {
    result.error = Context(sourcePath, std::string("image decode failed: ") + stbi_failure_reason());
    return result;
  }
  if (width <= 0 || height <= 0 ||
      static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / static_cast<size_t>(height) / 4) {
    stbi_image_free(pixels);
    result.error = Context(sourcePath, "decoded image dimensions are invalid or too large");
    return result;
  }
  const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  result.image.name = sourcePath.stem().string();
  result.image.mimeType = extension == ".png" ? "image/png" : "image/jpeg";
  result.image.width = static_cast<uint32_t>(width);
  result.image.height = static_cast<uint32_t>(height);
  result.image.rgba8.assign(pixels, pixels + byteCount);
  stbi_image_free(pixels);
  return result;
}
} // namespace Engine
