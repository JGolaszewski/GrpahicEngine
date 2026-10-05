#include "SceneDocument.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <type_traits>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace Engine {
namespace {
	using Json = nlohmann::json;
	std::atomic<uint64_t> temporaryFileSequence = 0;

	std::string FileContext(const std::filesystem::path& path, const std::string& message) {
		return path.generic_string() + ": " + message;
	}

	bool IsGuid(const std::string& value) {
		if (value.size() != 36) return false;
		for (size_t i = 0; i < value.size(); ++i) {
			if (i == 8 || i == 13 || i == 18 || i == 23) {
				if (value[i] != '-') return false;
			} else if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
				return false;
			}
		}
		return true;
	}

	template<size_t N>
	bool ReadFloatArray(const Json& value, std::array<float, N>& result, const std::string& field, std::string& error) {
		if (!value.is_array() || value.size() != N) {
			error = field + " must be an array of " + std::to_string(N) + " numbers";
			return false;
		}
		for (size_t i = 0; i < N; ++i) {
			if (!value[i].is_number()) {
				error = field + " contains a non-numeric value";
				return false;
			}
			const double number = value[i].get<double>();
			if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) {
				error = field + " contains a non-finite or unrepresentable value";
				return false;
			}
			result[i] = static_cast<float>(number);
		}
		return true;
	}

	template<typename T>
	bool ReadNumber(const Json& object, const char* key, T& result, std::string& error) {
		if (!object.contains(key)) return true;
		const Json& value = object.at(key);
		if (!value.is_number()) {
			error = std::string(key) + " must be numeric";
			return false;
		}
		const double number = value.get<double>();
		if (!std::isfinite(number) || number < -static_cast<double>(std::numeric_limits<T>::max()) ||
			number > static_cast<double>(std::numeric_limits<T>::max())) {
			error = std::string(key) + " contains a non-finite or unrepresentable value";
			return false;
		}
		result = static_cast<T>(number);
		return true;
	}

	bool ReadBool(const Json& object, const char* key, bool& result, std::string& error) {
		if (!object.contains(key)) return true;
		if (!object.at(key).is_boolean()) {
			error = std::string(key) + " must be boolean";
			return false;
		}
		result = object.at(key).get<bool>();
		return true;
	}

	bool ReadId(const Json& object, uint64_t& id, std::string& error) {
		if (!object.contains("id") || !object["id"].is_number_unsigned()) {
			error = "id must be an unsigned integer";
			return false;
		}
		id = object["id"].get<uint64_t>();
		if (id == 0) { error = "id must be positive"; return false; }
		return true;
	}

	template<size_t N>
	bool ReadArrayField(const Json& object, const char* key, std::array<float, N>& result, std::string& error) {
		return !object.contains(key) || ReadFloatArray(object.at(key), result, key, error);
	}

	bool ReadOptionalGuid(const Json& object, const char* key, std::optional<std::string>& result, std::string& error) {
		if (!object.contains(key) || object.at(key).is_null()) return true;
		if (!object.at(key).is_string()) {
			error = std::string(key) + " must be a GUID string or null";
			return false;
		}
		result = object.at(key).get<std::string>();
		if (!IsGuid(*result)) {
			error = std::string(key) + " is not a valid GUID";
			return false;
		}
		return true;
	}

	bool KnownKeys(const Json& object, std::initializer_list<const char*> known, std::vector<std::string>& warnings, const std::string& context) {
		if (!object.is_object()) return true;
		for (auto it = object.begin(); it != object.end(); ++it) {
			const bool found = std::any_of(known.begin(), known.end(), [&](const char* key) { return it.key() == key; });
			if (!found) warnings.push_back(context + " contains unknown field '" + it.key() + "' (ignored)");
		}
		return true;
	}

	Json ToJson(const SceneDocument& document) {
		Json result;
		result["schemaVersion"] = document.schemaVersion;
		result["unitsPerMeter"] = document.unitsPerMeter;
		result["coordinateSystem"] = "right-handed-y-up";
		result["entities"] = Json::array();
		for (const auto& entity : document.entities) {
			Json item = {
				{"id", entity.id}, {"name", entity.name},
				{"modelGuid", entity.modelGuid ? Json(*entity.modelGuid) : Json(nullptr)},
				{"materialGuid", entity.materialGuid ? Json(*entity.materialGuid) : Json(nullptr)},
				{"position", entity.position}, {"rotation", entity.rotation}, {"scale", entity.scale},
				{"render", {
					{"forceWireframe", entity.forceWireframe}, {"disableDepthTest", entity.disableDepthTest},
					{"castShadows", entity.castShadows}, {"receiveShadows", entity.receiveShadows}
				}}
			};
			result["entities"].push_back(std::move(item));
		}
		result["lights"] = Json::array();
		for (const auto& light : document.lights) {
			result["lights"].push_back({
				{"id", light.id}, {"direction", light.direction}, {"position", light.position}, {"color", light.color},
				{"intensity", light.intensity}, {"range", light.range},
				{"innerAngleDegrees", light.innerAngleDegrees}, {"outerAngleDegrees", light.outerAngleDegrees},
				{"shadowBias", light.shadowBias}, {"shadowNormalBias", light.shadowNormalBias},
				{"usePosition", light.usePosition}, {"castShadows", light.castShadows}
			});
		}
		result["cameras"] = Json::array();
		for (const auto& camera : document.cameras) {
			result["cameras"].push_back({
				{"id", camera.id}, {"position", camera.position}, {"rotation", camera.rotation},
				{"fovRadians", camera.fovRadians}, {"nearPlane", camera.nearPlane}, {"farPlane", camera.farPlane}
			});
		}
		return result;
	}
}

	SceneDocumentResult SceneSerializer::Load(const std::filesystem::path& path) const {
		SceneDocumentResult result;
		Json root;
		try {
			std::ifstream input(path, std::ios::binary);
			if (!input) {
				result.error = FileContext(path, "could not open scene file");
				return result;
			}
			input >> root;
		} catch (const Json::exception& exception) {
			result.error = FileContext(path, std::string("invalid JSON: ") + exception.what());
			return result;
		} catch (const std::ios_base::failure& exception) {
			result.error = FileContext(path, std::string("could not read scene file: ") + exception.what());
			return result;
		}
		if (!root.is_object()) {
			result.error = FileContext(path, "scene root must be an object");
			return result;
		}
		KnownKeys(root, {"schemaVersion", "coordinateSystem", "unitsPerMeter", "entities", "lights", "cameras", "editor"}, result.warnings, "scene");
		if (!root.contains("schemaVersion") || !root["schemaVersion"].is_number_unsigned()) {
			result.error = FileContext(path, "schemaVersion must be an unsigned integer");
			return result;
		}
		result.document.schemaVersion = root["schemaVersion"].get<uint32_t>();
		if (result.document.schemaVersion != CurrentSceneSchemaVersion) {
			result.error = FileContext(path, "unsupported scene schema version " + std::to_string(result.document.schemaVersion));
			return result;
		}
		if (root.contains("coordinateSystem") && (!root["coordinateSystem"].is_string() || root["coordinateSystem"] != "right-handed-y-up")) {
			result.error = FileContext(path, "coordinateSystem must be 'right-handed-y-up'");
			return result;
		}
		std::string fieldError;
		if (!ReadNumber(root, "unitsPerMeter", result.document.unitsPerMeter, fieldError) || result.document.unitsPerMeter <= 0.0f) {
			result.error = FileContext(path, fieldError.empty() ? "unitsPerMeter must be positive" : fieldError);
			return result;
		}
		std::set<uint64_t> entityIds;
		if (root.contains("entities")) {
			if (!root["entities"].is_array()) { result.error = FileContext(path, "entities must be an array"); return result; }
			for (size_t index = 0; index < root["entities"].size(); ++index) {
				const Json& item = root["entities"][index];
				const std::string context = "entities[" + std::to_string(index) + "]";
				if (!item.is_object()) { result.error = FileContext(path, context + " must be an object"); return result; }
				KnownKeys(item, {"id", "name", "modelGuid", "materialGuid", "position", "rotation", "scale", "render"}, result.warnings, context);
				SceneEntityDocument entity;
				if (!ReadId(item, entity.id, fieldError) || !entityIds.insert(entity.id).second) {
					result.error = FileContext(path, context + ".id must be a unique positive integer"); return result;
				}
				if (item.contains("name")) {
					if (!item["name"].is_string()) { result.error = FileContext(path, context + ".name must be a string"); return result; }
					entity.name = item["name"].get<std::string>();
				}
				if (!ReadOptionalGuid(item, "modelGuid", entity.modelGuid, fieldError) || !ReadOptionalGuid(item, "materialGuid", entity.materialGuid, fieldError)) {
					result.error = FileContext(path, context + "." + fieldError); return result;
				}
				if (item.contains("position") && !ReadFloatArray(item["position"], entity.position, context + ".position", fieldError)) { result.error = FileContext(path, fieldError); return result; }
				if (item.contains("rotation") && !ReadFloatArray(item["rotation"], entity.rotation, context + ".rotation", fieldError)) { result.error = FileContext(path, fieldError); return result; }
				if (item.contains("scale") && !ReadFloatArray(item["scale"], entity.scale, context + ".scale", fieldError)) { result.error = FileContext(path, fieldError); return result; }
				const float rotationLengthSquared = entity.rotation[0] * entity.rotation[0] + entity.rotation[1] * entity.rotation[1] + entity.rotation[2] * entity.rotation[2] + entity.rotation[3] * entity.rotation[3];
				if (!std::isfinite(rotationLengthSquared) || rotationLengthSquared <= 1.0e-12f) { result.error = FileContext(path, context + ".rotation must be a non-zero quaternion"); return result; }
				if (item.contains("render")) {
					const Json& render = item["render"];
					if (!render.is_object()) { result.error = FileContext(path, context + ".render must be an object"); return result; }
					KnownKeys(render, {"forceWireframe", "disableDepthTest", "castShadows", "receiveShadows"}, result.warnings, context + ".render");
					if (!ReadBool(render, "forceWireframe", entity.forceWireframe, fieldError) || !ReadBool(render, "disableDepthTest", entity.disableDepthTest, fieldError) || !ReadBool(render, "castShadows", entity.castShadows, fieldError) || !ReadBool(render, "receiveShadows", entity.receiveShadows, fieldError)) { result.error = FileContext(path, context + ".render." + fieldError); return result; }
				}
				result.document.entities.push_back(std::move(entity));
			}
		}
		std::set<uint64_t> lightIds;
		if (root.contains("lights")) {
			if (!root["lights"].is_array()) { result.error = FileContext(path, "lights must be an array"); return result; }
			for (size_t index = 0; index < root["lights"].size(); ++index) {
				const Json& item = root["lights"][index];
				const std::string context = "lights[" + std::to_string(index) + "]";
				if (!item.is_object()) { result.error = FileContext(path, context + " must be an object"); return result; }
				KnownKeys(item, {"id", "direction", "position", "color", "intensity", "range", "innerAngleDegrees", "outerAngleDegrees", "shadowBias", "shadowNormalBias", "usePosition", "castShadows"}, result.warnings, context);
				SceneLightDocument light;
				if (!ReadId(item, light.id, fieldError) || !lightIds.insert(light.id).second ||
					!ReadArrayField(item, "direction", light.direction, fieldError) ||
					!ReadArrayField(item, "position", light.position, fieldError) ||
					!ReadArrayField(item, "color", light.color, fieldError) ||
					!ReadNumber(item, "intensity", light.intensity, fieldError) ||
					!ReadNumber(item, "range", light.range, fieldError) ||
					!ReadNumber(item, "innerAngleDegrees", light.innerAngleDegrees, fieldError) ||
					!ReadNumber(item, "outerAngleDegrees", light.outerAngleDegrees, fieldError) ||
					!ReadNumber(item, "shadowBias", light.shadowBias, fieldError) ||
					!ReadNumber(item, "shadowNormalBias", light.shadowNormalBias, fieldError) ||
					!ReadBool(item, "usePosition", light.usePosition, fieldError) ||
					!ReadBool(item, "castShadows", light.castShadows, fieldError)) {
					result.error = FileContext(path, context + ": " + (fieldError.empty() ? "duplicate light ID" : fieldError)); return result;
				}
				const float directionLengthSquared = light.direction[0] * light.direction[0] + light.direction[1] * light.direction[1] + light.direction[2] * light.direction[2];
				if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= 1.0e-12f || light.color[0] < 0.0f || light.color[1] < 0.0f || light.color[2] < 0.0f || light.intensity < 0.0f || light.range <= 0.0f || light.innerAngleDegrees < 0.0f ||
					light.outerAngleDegrees < light.innerAngleDegrees || light.outerAngleDegrees > 89.5f ||
					light.shadowBias < 0.0f || light.shadowNormalBias < 0.0f) {
					result.error = FileContext(path, context + " contains out-of-range light settings"); return result;
				}
				result.document.lights.push_back(std::move(light));
			}
		}
		std::set<uint64_t> cameraIds;
		if (root.contains("cameras")) {
			if (!root["cameras"].is_array()) { result.error = FileContext(path, "cameras must be an array"); return result; }
			for (size_t index = 0; index < root["cameras"].size(); ++index) {
				const Json& item = root["cameras"][index];
				const std::string context = "cameras[" + std::to_string(index) + "]";
				if (!item.is_object()) { result.error = FileContext(path, context + " must be an object"); return result; }
				KnownKeys(item, {"id", "position", "rotation", "fovRadians", "nearPlane", "farPlane"}, result.warnings, context);
				SceneCameraDocument camera;
				if (!ReadId(item, camera.id, fieldError) || !cameraIds.insert(camera.id).second ||
					!ReadArrayField(item, "position", camera.position, fieldError) ||
					!ReadArrayField(item, "rotation", camera.rotation, fieldError) ||
					!ReadNumber(item, "fovRadians", camera.fovRadians, fieldError) ||
					!ReadNumber(item, "nearPlane", camera.nearPlane, fieldError) ||
					!ReadNumber(item, "farPlane", camera.farPlane, fieldError)) {
					result.error = FileContext(path, context + ": " + (fieldError.empty() ? "duplicate camera ID" : fieldError)); return result;
				}
				const float rotationLengthSquared = camera.rotation[0] * camera.rotation[0] + camera.rotation[1] * camera.rotation[1] + camera.rotation[2] * camera.rotation[2] + camera.rotation[3] * camera.rotation[3];
				if (camera.fovRadians <= 0.0f || camera.fovRadians >= 3.141592654f || camera.nearPlane <= 0.0f || camera.farPlane <= camera.nearPlane || rotationLengthSquared <= 1.0e-12f) {
					result.error = FileContext(path, context + " contains invalid projection or rotation values"); return result;
				}
				result.document.cameras.push_back(std::move(camera));
			}
		}
		return result;
	}

	bool SceneSerializer::SaveAtomic(const std::filesystem::path& path, const SceneDocument& document, std::string& error) const {
		error.clear();
		if (document.schemaVersion != CurrentSceneSchemaVersion || !std::isfinite(document.unitsPerMeter) || document.unitsPerMeter <= 0.0f) {
			error = FileContext(path, "document has an unsupported version or invalid unitsPerMeter");
			return false;
		}
		std::set<uint64_t> entityIds;
		for (const auto& entity : document.entities) {
			if (entity.id == 0 || !entityIds.insert(entity.id).second ||
				(entity.modelGuid && !IsGuid(*entity.modelGuid)) || (entity.materialGuid && !IsGuid(*entity.materialGuid))) {
				error = FileContext(path, "document contains a duplicate/invalid entity ID or asset GUID");
				return false;
			}
			const float values[] = { entity.position[0], entity.position[1], entity.position[2], entity.rotation[0], entity.rotation[1], entity.rotation[2], entity.rotation[3], entity.scale[0], entity.scale[1], entity.scale[2] };
			const float rotationLengthSquared = entity.rotation[0] * entity.rotation[0] + entity.rotation[1] * entity.rotation[1] + entity.rotation[2] * entity.rotation[2] + entity.rotation[3] * entity.rotation[3];
			if (!std::all_of(std::begin(values), std::end(values), [](float value) { return std::isfinite(value); }) || !std::isfinite(rotationLengthSquared) || rotationLengthSquared <= 1.0e-12f) { error = FileContext(path, "document contains an invalid entity transform"); return false; }
		}
		std::set<uint64_t> lightIds;
		for (const auto& light : document.lights) {
			const float values[] = { light.direction[0], light.direction[1], light.direction[2], light.position[0], light.position[1], light.position[2], light.color[0], light.color[1], light.color[2], light.intensity, light.range, light.innerAngleDegrees, light.outerAngleDegrees, light.shadowBias, light.shadowNormalBias };
			if (light.id == 0 || !lightIds.insert(light.id).second || !std::all_of(std::begin(values), std::end(values), [](float value) { return std::isfinite(value); }) || light.intensity < 0.0f || light.range <= 0.0f || light.innerAngleDegrees < 0.0f || light.outerAngleDegrees < light.innerAngleDegrees || light.outerAngleDegrees > 89.5f || light.shadowBias < 0.0f || light.shadowNormalBias < 0.0f) { error = FileContext(path, "document contains invalid light data"); return false; }
			const float directionLengthSquared = light.direction[0] * light.direction[0] + light.direction[1] * light.direction[1] + light.direction[2] * light.direction[2];
			if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= 1.0e-12f || light.color[0] < 0.0f || light.color[1] < 0.0f || light.color[2] < 0.0f) { error = FileContext(path, "document contains an invalid light direction or color"); return false; }
		}
		std::set<uint64_t> cameraIds;
		for (const auto& camera : document.cameras) {
			const float values[] = { camera.position[0], camera.position[1], camera.position[2], camera.rotation[0], camera.rotation[1], camera.rotation[2], camera.rotation[3], camera.fovRadians, camera.nearPlane, camera.farPlane };
			const float rotationLengthSquared = camera.rotation[0] * camera.rotation[0] + camera.rotation[1] * camera.rotation[1] + camera.rotation[2] * camera.rotation[2] + camera.rotation[3] * camera.rotation[3];
			if (camera.id == 0 || !cameraIds.insert(camera.id).second || !std::all_of(std::begin(values), std::end(values), [](float value) { return std::isfinite(value); }) || camera.fovRadians <= 0.0f || camera.fovRadians >= 3.141592654f || camera.nearPlane <= 0.0f || camera.farPlane <= camera.nearPlane || !std::isfinite(rotationLengthSquared) || rotationLengthSquared <= 1.0e-12f) { error = FileContext(path, "document contains invalid camera data"); return false; }
		}

		std::filesystem::path temporaryPath = path;
		temporaryPath += ".tmp." + std::to_string(temporaryFileSequence.fetch_add(1, std::memory_order_relaxed));
		try {
			if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
			{
				std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
				if (!output) { error = FileContext(path, "could not create temporary scene file"); std::error_code ignored; std::filesystem::remove(temporaryPath, ignored); return false; }
				output << ToJson(document).dump(2) << '\n';
				output.flush();
				if (!output) { error = FileContext(path, "could not write temporary scene file"); output.close(); std::filesystem::remove(temporaryPath); return false; }
			}
#if defined(_WIN32)
			if (!MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				const DWORD code = GetLastError();
				std::filesystem::remove(temporaryPath);
				error = FileContext(path, "atomic replacement failed with Windows error " + std::to_string(code));
				return false;
			}
#else
			std::filesystem::rename(temporaryPath, path);
#endif
			return true;
		} catch (const std::exception& exception) {
			std::error_code ignored;
			std::filesystem::remove(temporaryPath, ignored);
			error = FileContext(path, std::string("save failed: ") + exception.what());
			return false;
		}
	}
}
