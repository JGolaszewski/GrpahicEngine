#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include "AssetImporter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <sstream>

namespace Engine {
namespace {
	std::string Context(const std::filesystem::path& path, const std::string& message) {
		return path.generic_string() + ": " + message;
	}

	bool IsSupportedImage(const tinygltf::Image& image) {
		std::string mimeType = image.mimeType;
		std::transform(mimeType.begin(), mimeType.end(), mimeType.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (!mimeType.empty()) {
			return mimeType == "image/png" || mimeType == "image/jpeg" || mimeType == "image/jpg";
		}
		std::string extension = std::filesystem::path(image.uri).extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
	}

	bool GetAccessorData(const tinygltf::Model& model, int accessorIndex,
		const tinygltf::Accessor*& accessor, const uint8_t*& data, size_t& stride,
		std::string& error, const std::string& label) {
		if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
			error = label + " references an invalid accessor";
			return false;
		}
		accessor = &model.accessors[accessorIndex];
		if (accessor->sparse.isSparse) {
			error = label + " uses a sparse accessor, which is not supported yet";
			return false;
		}
		if (accessor->bufferView < 0 || static_cast<size_t>(accessor->bufferView) >= model.bufferViews.size()) {
			error = label + " has no valid buffer view";
			return false;
		}
		const tinygltf::BufferView& view = model.bufferViews[accessor->bufferView];
		if (view.buffer < 0 || static_cast<size_t>(view.buffer) >= model.buffers.size()) {
			error = label + " references an invalid buffer";
			return false;
		}
		const std::vector<unsigned char>& bytes = model.buffers[view.buffer].data;
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
			const size_t required = (accessor->count - 1) * stride + elementSize;
			if (required > view.byteLength - accessor->byteOffset || required > bytes.size() - start) {
				error = label + " accessor data is truncated";
				return false;
			}
		}
		data = bytes.data() + start;
		return true;
	}

	template<typename T>
	T ReadComponent(const uint8_t* source) {
		T value;
		std::memcpy(&value, source, sizeof(T));
		return value;
	}

	double ReadAttributeComponent(const uint8_t* source, int componentType, bool normalized) {
		switch (componentType) {
		case TINYGLTF_COMPONENT_TYPE_FLOAT: return ReadComponent<float>(source);
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
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return ReadComponent<uint32_t>(source);
		default: return std::numeric_limits<double>::quiet_NaN();
		}
	}

	bool ReadFloatAttribute(const tinygltf::Model& model, int accessorIndex, int expectedComponents,
		std::vector<std::array<float, 4>>& values, std::string& error, const std::string& label) {
		const tinygltf::Accessor* accessor = nullptr;
		const uint8_t* data = nullptr;
		size_t stride = 0;
		if (!GetAccessorData(model, accessorIndex, accessor, data, stride, error, label)) return false;
		if (tinygltf::GetNumComponentsInType(accessor->type) != expectedComponents) {
			error = label + " has an unexpected vector size";
			return false;
		}
		const int componentType = accessor->componentType;
		const bool floatingPoint = componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && !accessor->normalized;
		const bool normalizedSigned = accessor->normalized &&
			(componentType == TINYGLTF_COMPONENT_TYPE_BYTE || componentType == TINYGLTF_COMPONENT_TYPE_SHORT);
		const bool normalizedUnsigned = accessor->normalized &&
			(componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE || componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
		const bool validStorage = label == "POSITION" || label == "TANGENT"
			? floatingPoint
			: label == "NORMAL"
				? floatingPoint || normalizedSigned
				: floatingPoint || normalizedUnsigned;
		if (!validStorage) {
			error = label + " uses an invalid component type or normalization mode";
			return false;
		}
		const size_t componentSize = tinygltf::GetComponentSizeInBytes(accessor->componentType);
		values.resize(accessor->count);
		for (size_t i = 0; i < accessor->count; ++i) {
			for (int c = 0; c < expectedComponents; ++c) {
				const double value = ReadAttributeComponent(data + i * stride + static_cast<size_t>(c) * componentSize,
					accessor->componentType, accessor->normalized);
				if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
					error = label + " contains a non-finite or unrepresentable value";
					return false;
				}
				values[i][c] = static_cast<float>(value);
			}
		}
		return true;
	}

	bool ReadIndices(const tinygltf::Model& model, int accessorIndex, size_t vertexCount,
		std::vector<uint32_t>& indices, std::string& error) {
		const tinygltf::Accessor* accessor = nullptr;
		const uint8_t* data = nullptr;
		size_t stride = 0;
		if (!GetAccessorData(model, accessorIndex, accessor, data, stride, error, "indices")) return false;
		if (accessor->type != TINYGLTF_TYPE_SCALAR || accessor->normalized ||
			(accessor->componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
			 accessor->componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
			 accessor->componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)) {
			error = "indices must use an unsigned scalar accessor";
			return false;
		}
		indices.resize(accessor->count);
		for (size_t i = 0; i < accessor->count; ++i) {
			const uint8_t* item = data + i * stride;
			uint32_t index = 0;
			switch (accessor->componentType) {
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: index = ReadComponent<uint8_t>(item); break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: index = ReadComponent<uint16_t>(item); break;
			default: index = ReadComponent<uint32_t>(item); break;
			}
			if (index >= vertexCount) {
				error = "indices contains a vertex index outside the POSITION accessor";
				return false;
			}
			indices[i] = index;
		}
		return true;
	}

	DirectX::XMFLOAT3 Normalize(const DirectX::XMFLOAT3& v, const DirectX::XMFLOAT3& fallback) {
		const float lengthSquared = v.x * v.x + v.y * v.y + v.z * v.z;
		if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-20f) return fallback;
		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		return { v.x * inverseLength, v.y * inverseLength, v.z * inverseLength };
	}

	void GenerateNormals(ImportedPrimitive& primitive) {
		std::vector<DirectX::XMFLOAT3> accumulated(primitive.vertices.size());
		for (size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
			auto& a = primitive.vertices[primitive.indices[i]].Position;
			auto& b = primitive.vertices[primitive.indices[i + 1]].Position;
			auto& c = primitive.vertices[primitive.indices[i + 2]].Position;
			const DirectX::XMFLOAT3 edge1{ b.x - a.x, b.y - a.y, b.z - a.z };
			const DirectX::XMFLOAT3 edge2{ c.x - a.x, c.y - a.y, c.z - a.z };
			const DirectX::XMFLOAT3 normal{
				edge1.y * edge2.z - edge1.z * edge2.y,
				edge1.z * edge2.x - edge1.x * edge2.z,
				edge1.x * edge2.y - edge1.y * edge2.x };
			for (size_t corner = 0; corner < 3; ++corner) {
				auto& sum = accumulated[primitive.indices[i + corner]];
				sum.x += normal.x; sum.y += normal.y; sum.z += normal.z;
			}
		}
		for (size_t i = 0; i < primitive.vertices.size(); ++i)
			primitive.vertices[i].Normal = Normalize(accumulated[i], { 0.0f, 1.0f, 0.0f });
	}

	void GenerateTangents(ImportedPrimitive& primitive) {
		std::vector<DirectX::XMFLOAT3> tangent(primitive.vertices.size());
		std::vector<DirectX::XMFLOAT3> bitangent(primitive.vertices.size());
		for (size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
			const uint32_t ia = primitive.indices[i], ib = primitive.indices[i + 1], ic = primitive.indices[i + 2];
			const auto& a = primitive.vertices[ia]; const auto& b = primitive.vertices[ib]; const auto& c = primitive.vertices[ic];
			const float x1 = b.Position.x - a.Position.x, x2 = c.Position.x - a.Position.x;
			const float y1 = b.Position.y - a.Position.y, y2 = c.Position.y - a.Position.y;
			const float z1 = b.Position.z - a.Position.z, z2 = c.Position.z - a.Position.z;
			const float s1 = b.UV.x - a.UV.x, s2 = c.UV.x - a.UV.x;
			const float t1 = b.UV.y - a.UV.y, t2 = c.UV.y - a.UV.y;
			const float determinant = s1 * t2 - s2 * t1;
			if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-20f) continue;
			const float r = 1.0f / determinant;
			const DirectX::XMFLOAT3 sdir{ (t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r };
			const DirectX::XMFLOAT3 tdir{ (s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r };
			for (uint32_t index : { ia, ib, ic }) {
				tangent[index].x += sdir.x; tangent[index].y += sdir.y; tangent[index].z += sdir.z;
				bitangent[index].x += tdir.x; bitangent[index].y += tdir.y; bitangent[index].z += tdir.z;
			}
		}
		for (size_t i = 0; i < primitive.vertices.size(); ++i) {
			const auto& normal = primitive.vertices[i].Normal;
			const auto& source = tangent[i];
			const float projection = normal.x * source.x + normal.y * source.y + normal.z * source.z;
			const DirectX::XMFLOAT3 orthogonal{
				source.x - normal.x * projection,
				source.y - normal.y * projection,
				source.z - normal.z * projection };
			const DirectX::XMFLOAT3 fallback = std::abs(normal.y) < 0.9f
				? Normalize({ normal.z, 0.0f, -normal.x }, { 1.0f, 0.0f, 0.0f })
				: DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f };
			const auto direction = Normalize(orthogonal, fallback);
			const auto& bitan = bitangent[i];
			const float handedness = (normal.x * (direction.y * bitan.z - direction.z * bitan.y) +
				normal.y * (direction.z * bitan.x - direction.x * bitan.z) +
				normal.z * (direction.x * bitan.y - direction.y * bitan.x)) < 0.0f ? -1.0f : 1.0f;
			primitive.vertices[i].Tangent = { direction.x, direction.y, direction.z, handedness };
		}
	}

	bool ImportPrimitive(const tinygltf::Model& source, const tinygltf::Primitive& input,
		ImportedPrimitive& output, std::string& error) {
		if (input.mode != -1 && input.mode != TINYGLTF_MODE_TRIANGLES) {
			error = "only triangle-list glTF primitives are supported";
			return false;
		}
		const auto positionIt = input.attributes.find("POSITION");
		if (positionIt == input.attributes.end()) { error = "primitive is missing POSITION"; return false; }
		std::vector<std::array<float, 4>> positions, normals, uvs, tangents;
		if (!ReadFloatAttribute(source, positionIt->second, 3, positions, error, "POSITION")) return false;
		if (positions.empty() || positions.size() > std::numeric_limits<uint32_t>::max()) {
			error = "POSITION has an invalid vertex count"; return false;
		}
		const auto readOptional = [&](const char* name, int components, std::vector<std::array<float, 4>>& values) {
			const auto it = input.attributes.find(name);
			return it == input.attributes.end() || ReadFloatAttribute(source, it->second, components, values, error, name);
		};
		if (!readOptional("NORMAL", 3, normals) || !readOptional("TEXCOORD_0", 2, uvs) || !readOptional("TANGENT", 4, tangents)) return false;
		if ((!normals.empty() && normals.size() != positions.size()) || (!uvs.empty() && uvs.size() != positions.size()) ||
			(!tangents.empty() && tangents.size() != positions.size())) {
			error = "vertex attribute counts do not match POSITION"; return false;
		}
		output.vertices.resize(positions.size());
		for (size_t i = 0; i < positions.size(); ++i) {
			auto& vertex = output.vertices[i];
			vertex.Position = { positions[i][0], positions[i][1], positions[i][2] };
			if (!normals.empty()) vertex.Normal = Normalize({ normals[i][0], normals[i][1], normals[i][2] }, { 0.0f, 1.0f, 0.0f });
			if (!uvs.empty()) vertex.UV = { uvs[i][0], uvs[i][1] };
			if (!tangents.empty()) {
				const auto direction = Normalize({ tangents[i][0], tangents[i][1], tangents[i][2] }, { 1.0f, 0.0f, 0.0f });
				vertex.Tangent = { direction.x, direction.y, direction.z, tangents[i][3] < 0.0f ? -1.0f : 1.0f };
			}
		}
		if (input.indices < 0) {
			output.indices.resize(output.vertices.size());
			for (size_t i = 0; i < output.indices.size(); ++i) output.indices[i] = static_cast<uint32_t>(i);
		} else if (!ReadIndices(source, input.indices, output.vertices.size(), output.indices, error)) {
			return false;
		}
		if (output.indices.size() < 3 || output.indices.size() % 3 != 0) {
			error = "triangle primitive index count must be a non-zero multiple of three"; return false;
		}
		output.materialIndex = input.material;
		output.boundsMin = output.boundsMax = output.vertices.front().Position;
		for (const auto& vertex : output.vertices) {
			const auto& p = vertex.Position;
			output.boundsMin.x = (std::min)(output.boundsMin.x, p.x); output.boundsMin.y = (std::min)(output.boundsMin.y, p.y); output.boundsMin.z = (std::min)(output.boundsMin.z, p.z);
			output.boundsMax.x = (std::max)(output.boundsMax.x, p.x); output.boundsMax.y = (std::max)(output.boundsMax.y, p.y); output.boundsMax.z = (std::max)(output.boundsMax.z, p.z);
		}
		if (normals.empty()) GenerateNormals(output);
		if (tangents.empty()) GenerateTangents(output);
		return true;
	}

	bool ValidTextureIndex(int index, size_t size) { return index < 0 || static_cast<size_t>(index) < size; }
}

	AssetImportResult AssetImporter::ImportGltf(const std::filesystem::path& sourcePath) const {
		AssetImportResult result;
		const std::string extension = sourcePath.extension().string();
		std::string lowerExtension = extension;
		std::transform(lowerExtension.begin(), lowerExtension.end(), lowerExtension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (lowerExtension != ".gltf" && lowerExtension != ".glb") {
			result.error = Context(sourcePath, "expected a .gltf or .glb file");
			return result;
		}

		tinygltf::TinyGLTF loader;
		tinygltf::Model source;
		std::string warning, error;
		const bool loaded = lowerExtension == ".glb"
			? loader.LoadBinaryFromFile(&source, &error, &warning, sourcePath.string())
			: loader.LoadASCIIFromFile(&source, &error, &warning, sourcePath.string());
		if (!warning.empty()) result.warnings.push_back(Context(sourcePath, warning));
		if (!loaded) {
			result.error = Context(sourcePath, error.empty() ? "glTF parsing failed" : error);
			return result;
		}
		for (const auto& extensionName : source.extensionsRequired) {
			result.error = Context(sourcePath, "required glTF extension is unsupported: " + extensionName);
			return result;
		}
		for (const auto& extensionName : source.extensionsUsed) {
			result.warnings.push_back(Context(sourcePath, "optional glTF extension is ignored: " + extensionName));
		}
		if (!source.skins.empty()) result.warnings.push_back(Context(sourcePath, "skinning is not imported; bind-pose geometry is used"));
		if (!source.animations.empty()) result.warnings.push_back(Context(sourcePath, "animations are not imported"));

		result.model.sourcePath = sourcePath;
		result.model.defaultScene = source.defaultScene;
		result.model.scenes.reserve(source.scenes.size());
		for (const auto& scene : source.scenes) {
			std::vector<uint32_t> roots;
			for (int node : scene.nodes) {
				if (node < 0 || static_cast<size_t>(node) >= source.nodes.size()) {
					result.error = Context(sourcePath, "scene references an invalid node"); return result;
				}
				roots.push_back(static_cast<uint32_t>(node));
			}
			result.model.scenes.push_back(std::move(roots));
		}
		if (result.model.defaultScene < -1 || (result.model.defaultScene >= 0 && static_cast<size_t>(result.model.defaultScene) >= result.model.scenes.size())) {
			result.error = Context(sourcePath, "default scene index is invalid"); return result;
		}

		result.model.nodes.resize(source.nodes.size());
		for (size_t i = 0; i < source.nodes.size(); ++i) {
			const auto& input = source.nodes[i];
			auto& node = result.model.nodes[i];
			node.name = input.name;
			if ((!input.matrix.empty() && input.matrix.size() != 16) ||
				(!input.translation.empty() && input.translation.size() != 3) ||
				(!input.rotation.empty() && input.rotation.size() != 4) ||
				(!input.scale.empty() && input.scale.size() != 3) ||
				(!input.matrix.empty() && (!input.translation.empty() || !input.rotation.empty() || !input.scale.empty()))) {
				result.error = Context(sourcePath, "node has malformed or conflicting transform fields"); return result;
			}
			if (input.mesh >= 0 && static_cast<size_t>(input.mesh) >= source.meshes.size()) { result.error = Context(sourcePath, "node references an invalid mesh"); return result; }
			node.meshIndex = input.mesh;
			std::array<float, 16> matrixValues{};
			if (input.matrix.size() == 16) {
				node.hasMatrix = true;
				float* matrixMembers[] = {
					&node.matrix._11, &node.matrix._12, &node.matrix._13, &node.matrix._14,
					&node.matrix._21, &node.matrix._22, &node.matrix._23, &node.matrix._24,
					&node.matrix._31, &node.matrix._32, &node.matrix._33, &node.matrix._34,
					&node.matrix._41, &node.matrix._42, &node.matrix._43, &node.matrix._44
				};
				for (size_t n = 0; n < 16; ++n) {
					if (!std::isfinite(input.matrix[n]) || std::abs(input.matrix[n]) > std::numeric_limits<float>::max()) {
						result.error = Context(sourcePath, "node matrix contains a non-finite or unrepresentable value"); return result;
					}
					matrixValues[n] = static_cast<float>(input.matrix[n]);
					*matrixMembers[n] = matrixValues[n];
				}
			} else {
				if (!input.translation.empty()) node.translation = { static_cast<float>(input.translation[0]), static_cast<float>(input.translation[1]), static_cast<float>(input.translation[2]) };
				if (!input.rotation.empty()) node.rotation = { static_cast<float>(input.rotation[0]), static_cast<float>(input.rotation[1]), static_cast<float>(input.rotation[2]), static_cast<float>(input.rotation[3]) };
				if (!input.scale.empty()) node.scale = { static_cast<float>(input.scale[0]), static_cast<float>(input.scale[1]), static_cast<float>(input.scale[2]) };
			}
			const float transformValues[] = { node.translation.x, node.translation.y, node.translation.z,
				node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w,
				node.scale.x, node.scale.y, node.scale.z };
			if (!std::all_of(std::begin(transformValues), std::end(transformValues), [](float value) { return std::isfinite(value); })) {
				result.error = Context(sourcePath, "node transform contains a non-finite value"); return result;
			}
		}
		for (size_t i = 0; i < source.nodes.size(); ++i) {
			const auto& input = source.nodes[i];
			auto& node = result.model.nodes[i];
			for (int child : input.children) {
				if (child < 0 || static_cast<size_t>(child) >= source.nodes.size() || child == static_cast<int>(i) || result.model.nodes[child].parentIndex != -1) {
					result.error = Context(sourcePath, "node hierarchy contains an invalid or multiply-parented child"); return result;
				}
				result.model.nodes[child].parentIndex = static_cast<int32_t>(i);
				node.children.push_back(static_cast<uint32_t>(child));
			}
		}
		std::vector<uint8_t> visitState(result.model.nodes.size(), 0);
		const auto visitNode = [&](const auto& self, size_t index) -> bool {
			if (visitState[index] == 1) return false;
			if (visitState[index] == 2) return true;
			visitState[index] = 1;
			for (uint32_t child : result.model.nodes[index].children) if (!self(self, child)) return false;
			visitState[index] = 2;
			return true;
		};
		for (size_t i = 0; i < result.model.nodes.size(); ++i) {
			if (visitState[i] == 0 && !visitNode(visitNode, i)) {
				result.error = Context(sourcePath, "node hierarchy contains a cycle"); return result;
			}
		}
		for (const auto& scene : result.model.scenes) {
			for (uint32_t root : scene) {
				if (result.model.nodes[root].parentIndex != -1) {
					result.error = Context(sourcePath, "scene root is also a child node"); return result;
				}
			}
		}

		result.model.materials.reserve(source.materials.size());
		for (const auto& input : source.materials) {
			ImportedMaterial material;
			material.name = input.name;
			if (input.pbrMetallicRoughness.baseColorFactor.size() == 4)
				material.baseColor = { static_cast<float>(input.pbrMetallicRoughness.baseColorFactor[0]), static_cast<float>(input.pbrMetallicRoughness.baseColorFactor[1]), static_cast<float>(input.pbrMetallicRoughness.baseColorFactor[2]), static_cast<float>(input.pbrMetallicRoughness.baseColorFactor[3]) };
			if (input.emissiveFactor.size() == 3)
				material.emissive = { static_cast<float>(input.emissiveFactor[0]), static_cast<float>(input.emissiveFactor[1]), static_cast<float>(input.emissiveFactor[2]) };
			material.metallic = static_cast<float>(input.pbrMetallicRoughness.metallicFactor);
			material.roughness = static_cast<float>(input.pbrMetallicRoughness.roughnessFactor);
			material.alphaMode = input.alphaMode;
			if (material.alphaMode != "OPAQUE" && material.alphaMode != "MASK" && material.alphaMode != "BLEND") {
				result.error = Context(sourcePath, "material contains an invalid alpha mode"); return result;
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
			if (material.baseColorTexCoord != 0 || material.metallicRoughnessTexCoord != 0 ||
				material.normalTexCoord != 0 || material.occlusionTexCoord != 0 || material.emissiveTexCoord != 0) {
				result.error = Context(sourcePath, "only TEXCOORD_0 material texture coordinates are supported"); return result;
			}
			result.model.materials.push_back(std::move(material));
		}

		result.model.textures.reserve(source.textures.size());
		for (const auto& input : source.textures) {
			if (input.source < 0 || static_cast<size_t>(input.source) >= source.images.size()) { result.error = Context(sourcePath, "texture references an invalid image"); return result; }
			if (input.sampler >= 0 && static_cast<size_t>(input.sampler) >= source.samplers.size()) { result.error = Context(sourcePath, "texture references an invalid sampler"); return result; }
			ImportedTexture texture;
			texture.imageIndex = input.source;
			texture.samplerIndex = input.sampler;
			if (input.sampler >= 0) {
				const auto& sampler = source.samplers[input.sampler];
				texture.magFilter = sampler.magFilter;
				texture.minFilter = sampler.minFilter;
				texture.wrapS = sampler.wrapS;
				texture.wrapT = sampler.wrapT;
			}
			result.model.textures.push_back(texture);
		}
		for (const auto& material : result.model.materials) {
			for (int texture : { material.baseColorTexture, material.metallicRoughnessTexture, material.normalTexture, material.occlusionTexture, material.emissiveTexture }) {
				if (!ValidTextureIndex(texture, result.model.textures.size())) { result.error = Context(sourcePath, "material references an invalid texture"); return result; }
			}
		}

		result.model.images.reserve(source.images.size());
		for (const auto& input : source.images) {
			if (!IsSupportedImage(input)) { result.error = Context(sourcePath, "image format is unsupported: " + (input.mimeType.empty() ? input.uri : input.mimeType)); return result; }
			if (input.width <= 0 || input.height <= 0 || input.component <= 0 || input.bits != 8 || input.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
				result.error = Context(sourcePath, "image decoder returned an unsupported pixel layout"); return result;
			}
			const size_t width = static_cast<size_t>(input.width), height = static_cast<size_t>(input.height);
			if (width > std::numeric_limits<size_t>::max() / height || width * height > std::numeric_limits<size_t>::max() / 4 || input.image.size() != width * height * 4) {
				result.error = Context(sourcePath, "image pixel data has an invalid size"); return result;
			}
			ImportedImage image;
			image.name = input.name;
			image.mimeType = input.mimeType;
			image.width = static_cast<uint32_t>(input.width);
			image.height = static_cast<uint32_t>(input.height);
			image.rgba8.assign(input.image.begin(), input.image.end());
			result.model.images.push_back(std::move(image));
		}

		result.model.meshes.reserve(source.meshes.size());
		for (const auto& input : source.meshes) {
			ImportedMesh mesh;
			mesh.name = input.name;
			mesh.primitives.reserve(input.primitives.size());
			for (const auto& primitive : input.primitives) {
				if (primitive.material < -1 || (primitive.material >= 0 && static_cast<size_t>(primitive.material) >= result.model.materials.size())) {
					result.error = Context(sourcePath, "primitive references an invalid material"); return result;
				}
				ImportedPrimitive imported;
				imported.name = mesh.name;
				if (!ImportPrimitive(source, primitive, imported, error)) { result.error = Context(sourcePath, error); return result; }
				mesh.primitives.push_back(std::move(imported));
			}
			result.model.meshes.push_back(std::move(mesh));
		}
		return result;
	}
}
