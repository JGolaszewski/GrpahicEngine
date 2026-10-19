#include "rendering/include/persistence/SnapshotPersistence.h"

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4702) // OpenImageIO templates can trigger MSVC's unreachable-code warning.
#endif
#include <OpenImageIO/imageio.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <Windows.h>
#endif

namespace Engine::SnapshotPersistence {
namespace {
using Json = nlohmann::json;
constexpr std::string_view MetadataAttribute = "engine:snapshot_metadata_v1";
constexpr std::array<RenderAov, 5> Aovs = {RenderAov::FinalColor, RenderAov::Depth, RenderAov::WorldNormal,
                                           RenderAov::SpecularLighting, RenderAov::DiffuseLighting};

auto Context(const std::filesystem::path& path, std::string_view message) -> Error {
  return MakeError(path.generic_string() + ": " + std::string(message));
}

auto CheckedPixels(uint32_t width, uint32_t height) -> std::optional<size_t> {
  const uint64_t count = static_cast<uint64_t>(width) * height;
  if (width == 0 || height == 0 || width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      count > std::numeric_limits<size_t>::max() / 4U) {
    return std::nullopt;
  }
  return static_cast<size_t>(width) * height;
}

auto MetadataJson(const SnapshotFrame& frame) -> Json {
  const auto& m = frame.GetMetadata();
  const auto& c = m.Camera;
  const auto& r = m.RenderSettings;
  const auto& color = m.Color;
  Json manifest = Json::array();
  for (const auto& item : frame.GetManifest()) {
    const auto& v = item.CapturedValues;
    manifest.push_back({{"id", item.Id},
                        {"entity", item.EntityIdentity},
                        {"submesh", item.SubmeshIndex},
                        {"slot", item.MaterialSlot},
                        {"material", item.MaterialIdentity},
                        {"resources", item.ResourceIdentities},
                        {"values",
                         {{"baseColor", v.baseColor},
                          {"emissive", v.emissive},
                          {"metallic", v.metallic},
                          {"roughness", v.roughness},
                          {"gtrGamma", v.gtrGamma},
                          {"ior", v.ior},
                          {"specularF0", v.specularF0},
                          {"brdfModel", v.brdfModel},
                          {"workflow", v.workflow},
                          {"specularMode", v.specularMode},
                          {"alphaMode", v.alphaMode},
                          {"alphaCutoff", v.alphaCutoff},
                          {"doubleSided", v.doubleSided},
                          {"baseColorTextureGuid", v.baseColorTextureGuid},
                          {"normalTextureGuid", v.normalTextureGuid}}}});
  }
  return {{"schema", 1},
          {"captureTimeUtc", m.CaptureTimeUtc},
          {"sceneIdentity", m.SceneIdentity},
          {"camera",
           {{"identity", c.Identity},
            {"position", c.Position},
            {"rotation", c.Rotation},
            {"projection", "perspective"},
            {"fieldOfViewDegrees", c.FieldOfViewDegrees},
            {"nearPlane", c.NearPlane},
            {"farPlane", c.FarPlane},
            {"authoredWidth", c.AuthoredWidth},
            {"authoredHeight", c.AuthoredHeight}}},
          {"width", frame.GetWidth()},
          {"height", frame.GetHeight()},
          {"renderSettings",
           {{"shadowsEnabled", r.ShadowsEnabled},
            {"shadowMapResolution", r.ShadowMapResolution},
            {"maxShadowCasters", r.MaxShadowCasters},
            {"sampleCount", r.SampleCount}}},
          {"color",
           {{"primaries", "Rec709"},
            {"transfer", "linear"},
            {"luminanceUnit", color.LuminanceUnit == SnapshotLuminanceUnit::Nits ? "nits" : "scene-linear-relative"},
            {"nitsPerSceneUnit", color.NitsPerSceneUnit}}},
          {"materialManifest", std::move(manifest)}};
}

template <typename T> auto ReadValue(const Json& object, const char* key, T& target) -> bool {
  if (!object.contains(key) || !object.at(key).is_number())
    return false;
  const double value = object.at(key).get<double>();
  if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<T>::lowest()) ||
      value > static_cast<double>(std::numeric_limits<T>::max()))
    return false;
  target = static_cast<T>(value);
  return true;
}

template <size_t N> auto ReadArray(const Json& object, const char* key, std::array<float, N>& result) -> bool {
  if (!object.contains(key) || !object.at(key).is_array() || object.at(key).size() != N)
    return false;
  for (size_t index = 0; index < N; ++index) {
    if (!object.at(key).at(index).is_number())
      return false;
    const double value = object.at(key).at(index).get<double>();
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
      return false;
    result[index] = static_cast<float>(value);
  }
  return true;
}

auto ReadString(const Json& object, const char* key, std::string& result) -> bool {
  if (!object.contains(key) || !object.at(key).is_string())
    return false;
  result = object.at(key).get<std::string>();
  return true;
}

auto ParseMetadata(const Json& j, SnapshotFrameMetadata& m, std::vector<SnapshotMaterialManifestEntry>& manifest)
    -> bool {
  if (!j.is_object() || !j.contains("schema") || j.at("schema") != 1 || !j.contains("camera") ||
      !j.at("camera").is_object() || !j.contains("renderSettings") || !j.at("renderSettings").is_object() ||
      !j.contains("color") || !j.at("color").is_object() || !j.contains("materialManifest") ||
      !j.at("materialManifest").is_array())
    return false;
  const auto& camera = j.at("camera");
  const auto& settings = j.at("renderSettings");
  const auto& color = j.at("color");
  if (!ReadString(j, "captureTimeUtc", m.CaptureTimeUtc) || !ReadString(j, "sceneIdentity", m.SceneIdentity) ||
      !ReadValue(j, "width", m.OutputWidth) || !ReadValue(j, "height", m.OutputHeight) ||
      !ReadString(camera, "identity", m.Camera.Identity) || !ReadArray(camera, "position", m.Camera.Position) ||
      !ReadArray(camera, "rotation", m.Camera.Rotation) || !camera.contains("projection") ||
      camera.at("projection") != "perspective" ||
      !ReadValue(camera, "fieldOfViewDegrees", m.Camera.FieldOfViewDegrees) ||
      !ReadValue(camera, "nearPlane", m.Camera.NearPlane) || !ReadValue(camera, "farPlane", m.Camera.FarPlane) ||
      !ReadValue(camera, "authoredWidth", m.Camera.AuthoredWidth) ||
      !ReadValue(camera, "authoredHeight", m.Camera.AuthoredHeight) || !settings.contains("shadowsEnabled") ||
      !settings.at("shadowsEnabled").is_boolean() ||
      !ReadValue(settings, "shadowMapResolution", m.RenderSettings.ShadowMapResolution) ||
      !ReadValue(settings, "maxShadowCasters", m.RenderSettings.MaxShadowCasters) ||
      !ReadValue(settings, "sampleCount", m.RenderSettings.SampleCount) || !color.contains("primaries") ||
      color.at("primaries") != "Rec709" || !color.contains("transfer") || color.at("transfer") != "linear" ||
      !color.contains("luminanceUnit") || !color.at("luminanceUnit").is_string())
    return false;
  m.Camera.Projection = SnapshotProjectionType::Perspective;
  m.RenderSettings.ShadowsEnabled = settings.at("shadowsEnabled").get<bool>();
  const std::string unit = color.at("luminanceUnit").get<std::string>();
  if (unit == "nits")
    m.Color.LuminanceUnit = SnapshotLuminanceUnit::Nits;
  else if (unit == "scene-linear-relative")
    m.Color.LuminanceUnit = SnapshotLuminanceUnit::SceneLinearRelative;
  else
    return false;
  if (!color.contains("nitsPerSceneUnit"))
    return false;
  if (!color.at("nitsPerSceneUnit").is_null()) {
    float scale = 0.0F;
    Json holder{{"scale", color.at("nitsPerSceneUnit")}};
    if (!ReadValue(holder, "scale", scale))
      return false;
    m.Color.NitsPerSceneUnit = scale;
  }
  for (const auto& item : j.at("materialManifest")) {
    if (!item.is_object() || !item.contains("id") || !item.at("id").is_number_unsigned() || !item.contains("submesh") ||
        !item.at("submesh").is_number_unsigned() || !item.contains("slot") || !item.at("slot").is_number_unsigned() ||
        !item.contains("resources") || !item.at("resources").is_array() || !item.contains("values") ||
        !item.at("values").is_object())
      return false;
    SnapshotMaterialManifestEntry entry;
    entry.Id = item.at("id").get<uint32_t>();
    entry.SubmeshIndex = item.at("submesh").get<uint32_t>();
    entry.MaterialSlot = item.at("slot").get<uint32_t>();
    if (!ReadString(item, "entity", entry.EntityIdentity) || !ReadString(item, "material", entry.MaterialIdentity))
      return false;
    for (const auto& resource : item.at("resources")) {
      if (!resource.is_string())
        return false;
      entry.ResourceIdentities.push_back(resource.get<std::string>());
    }
    const auto& v = item.at("values");
    auto& d = entry.CapturedValues;
    if (!ReadArray(v, "baseColor", d.baseColor) || !ReadArray(v, "emissive", d.emissive) ||
        !ReadValue(v, "metallic", d.metallic) || !ReadValue(v, "roughness", d.roughness) ||
        !ReadValue(v, "gtrGamma", d.gtrGamma) || !ReadValue(v, "ior", d.ior) ||
        !ReadArray(v, "specularF0", d.specularF0) || !ReadString(v, "brdfModel", d.brdfModel) ||
        !ReadString(v, "workflow", d.workflow) || !ReadString(v, "specularMode", d.specularMode) ||
        !ReadString(v, "alphaMode", d.alphaMode) || !ReadValue(v, "alphaCutoff", d.alphaCutoff) ||
        !v.contains("doubleSided") || !v.at("doubleSided").is_boolean() || !v.contains("baseColorTextureGuid") ||
        !v.contains("normalTextureGuid"))
      return false;
    d.doubleSided = v.at("doubleSided").get<bool>();
    if (!v.at("baseColorTextureGuid").is_null()) {
      if (!v.at("baseColorTextureGuid").is_string())
        return false;
      d.baseColorTextureGuid = v.at("baseColorTextureGuid").get<std::string>();
    }
    if (!v.at("normalTextureGuid").is_null()) {
      if (!v.at("normalTextureGuid").is_string())
        return false;
      d.normalTextureGuid = v.at("normalTextureGuid").get<std::string>();
    }
    manifest.push_back(std::move(entry));
  }
  return true;
}

auto TempPath(const std::filesystem::path& path) -> std::filesystem::path {
  return path.parent_path() / (path.stem().string() + ".tmp" + path.extension().string());
}

auto AtomicReplace(const std::filesystem::path& temporary, const std::filesystem::path& destination) -> bool {
#ifdef _WIN32
  return MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  return !error;
#endif
}

auto ValidExtension(const std::filesystem::path& path, std::string_view extension) -> bool {
  std::string value = path.extension().string();
  std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value == extension;
}

auto AovIndex(std::string_view name) -> std::optional<size_t> {
  for (size_t i = 0; i < Aovs.size(); ++i)
    if (name == RenderOutput::GetDescriptor(Aovs[i]).Name)
      return i;
  return std::nullopt;
}

auto IsValidAov(RenderAov aov) -> bool {
  return std::ranges::any_of(Aovs, [aov](RenderAov candidate) { return candidate == aov; });
}

auto WorkspaceJson(const SnapshotWorkspaceDocument& document, const std::filesystem::path& path) -> Json {
  Json tabs = Json::array();
  const auto base = path.parent_path();
  for (const auto& tab : document.Tabs) {
    std::filesystem::path ref = tab.ExrPath;
    if (ref.is_absolute()) {
      std::error_code error;
      auto relative = std::filesystem::relative(ref, base, error);
      if (!error && !relative.empty())
        ref = relative;
    }
    Json pin = tab.PinnedPixel ? Json{{"x", tab.PinnedPixel->X}, {"y", tab.PinnedPixel->Y}} : Json(nullptr);
    Json roi = tab.Roi
                   ? Json{{"x", tab.Roi->X}, {"y", tab.Roi->Y}, {"width", tab.Roi->Width}, {"height", tab.Roi->Height}}
                   : Json(nullptr);
    tabs.push_back({{"exr", ref.generic_string()},
                    {"aov", RenderOutput::GetDescriptor(tab.SelectedAov).Name},
                    {"pin", std::move(pin)},
                    {"roi", std::move(roi)},
                    {"zoom", tab.Zoom},
                    {"pan", {tab.PanX, tab.PanY}}});
  }
  Json comparison = nullptr;
  if (document.Comparison) {
    const char* metric = "absoluteError";
    if (document.Comparison->Metric == SnapshotComparisonMetric::RgbRms)
      metric = "rgbRms";
    if (document.Comparison->Metric == SnapshotComparisonMetric::RelativeLuminance)
      metric = "relativeLuminance";
    comparison = {{"referenceTab", document.Comparison->ReferenceTab},
                  {"currentTab", document.Comparison->CurrentTab},
                  {"metric", metric},
                  {"wipePosition", document.Comparison->WipePosition}};
  }
  return {{"format", "GraphicEngineSnapshotWorkspace"},
          {"version", document.Version},
          {"selectedTab", document.SelectedTab},
          {"tabs", std::move(tabs)},
          {"comparison", std::move(comparison)}};
}

auto ValidateDocument(const SnapshotWorkspaceDocument& document) -> Status {
  if (document.Version != SnapshotWorkspaceDocument::CurrentVersion)
    return std::unexpected(MakeError("unsupported snapshot workspace version"));
  if (document.Tabs.empty() || document.SelectedTab >= document.Tabs.size())
    return std::unexpected(MakeError("workspace must have tabs and a valid selected tab"));
  for (const auto& tab : document.Tabs) {
    if (tab.ExrPath.empty() || !ValidExtension(tab.ExrPath, ".exr") || !IsValidAov(tab.SelectedAov) ||
        !std::isfinite(tab.Zoom) || tab.Zoom <= 0.0F || !std::isfinite(tab.PanX) || !std::isfinite(tab.PanY))
      return std::unexpected(MakeError("workspace tab has an invalid EXR path or view state"));
    if (tab.PinnedPixel && (tab.PinnedPixel->X >= 1000000000U || tab.PinnedPixel->Y >= 1000000000U))
      return std::unexpected(MakeError("workspace pinned pixel is out of supported range"));
    if (tab.Roi && (tab.Roi->Width == 0 || tab.Roi->Height == 0 ||
                    static_cast<uint64_t>(tab.Roi->X) + tab.Roi->Width > std::numeric_limits<uint32_t>::max() ||
                    static_cast<uint64_t>(tab.Roi->Y) + tab.Roi->Height > std::numeric_limits<uint32_t>::max()))
      return std::unexpected(MakeError("workspace ROI is invalid"));
  }
  if (document.Comparison) {
    const auto& c = *document.Comparison;
    if (c.ReferenceTab >= document.Tabs.size() || c.CurrentTab >= document.Tabs.size() ||
        c.ReferenceTab == c.CurrentTab || !std::isfinite(c.WipePosition) || c.WipePosition < 0.0F ||
        c.WipePosition > 1.0F ||
        (c.Metric != SnapshotComparisonMetric::AbsoluteError && c.Metric != SnapshotComparisonMetric::RgbRms &&
         c.Metric != SnapshotComparisonMetric::RelativeLuminance))
      return std::unexpected(MakeError("workspace comparison state is invalid"));
  }
  return {};
}

auto ReadWorkspaceJson(const Json& j, const std::filesystem::path& path) -> Result<SnapshotWorkspaceDocument> {
  SnapshotWorkspaceDocument document;
  try {
    if (!j.is_object() || !j.contains("format") || j.at("format") != "GraphicEngineSnapshotWorkspace" ||
        !j.contains("version") || !j.at("version").is_number_unsigned() || !j.contains("tabs") ||
        !j.at("tabs").is_array() || !j.contains("selectedTab") || !j.at("selectedTab").is_number_unsigned() ||
        !j.contains("comparison"))
      return std::unexpected(Context(path, "malformed workspace document"));
    document.Version = j.at("version").get<uint32_t>();
    document.SelectedTab = j.at("selectedTab").get<uint32_t>();
    for (const auto& item : j.at("tabs")) {
      if (!item.is_object() || !item.contains("exr") || !item.at("exr").is_string() || !item.contains("aov") ||
          !item.at("aov").is_string() || !item.contains("pin") || !item.contains("roi") || !item.contains("zoom") ||
          !item.at("zoom").is_number() || !item.contains("pan") || !item.at("pan").is_array() ||
          item.at("pan").size() != 2)
        return std::unexpected(Context(path, "malformed tab state"));
      SnapshotFrameViewState tab;
      tab.ExrPath = item.at("exr").get<std::string>();
      auto aov = AovIndex(item.at("aov").get<std::string>());
      if (!aov)
        return std::unexpected(Context(path, "workspace refers to unsupported AOV"));
      tab.SelectedAov = Aovs[*aov];
      tab.Zoom = item.at("zoom").get<float>();
      if (!item.at("pan").at(0).is_number() || !item.at("pan").at(1).is_number())
        return std::unexpected(Context(path, "invalid pan state"));
      tab.PanX = item.at("pan").at(0).get<float>();
      tab.PanY = item.at("pan").at(1).get<float>();
      if (!item.at("pin").is_null()) {
        const auto& pin = item.at("pin");
        if (!pin.is_object() || !pin.contains("x") || !pin.at("x").is_number_unsigned() || !pin.contains("y") ||
            !pin.at("y").is_number_unsigned())
          return std::unexpected(Context(path, "invalid pinned pixel"));
        tab.PinnedPixel = SnapshotPinnedPixelState{pin.at("x").get<uint32_t>(), pin.at("y").get<uint32_t>()};
      }
      if (!item.at("roi").is_null()) {
        const auto& roi = item.at("roi");
        if (!roi.is_object() || !roi.contains("x") || !roi.at("x").is_number_unsigned() || !roi.contains("y") ||
            !roi.at("y").is_number_unsigned() || !roi.contains("width") || !roi.at("width").is_number_unsigned() ||
            !roi.contains("height") || !roi.at("height").is_number_unsigned())
          return std::unexpected(Context(path, "invalid ROI state"));
        tab.Roi = SnapshotRoiState{roi.at("x").get<uint32_t>(), roi.at("y").get<uint32_t>(),
                                   roi.at("width").get<uint32_t>(), roi.at("height").get<uint32_t>()};
      }
      document.Tabs.push_back(std::move(tab));
    }
    if (!j.at("comparison").is_null()) {
      const auto& c = j.at("comparison");
      if (!c.is_object() || !c.contains("referenceTab") || !c.at("referenceTab").is_number_unsigned() ||
          !c.contains("currentTab") || !c.at("currentTab").is_number_unsigned() || !c.contains("metric") ||
          !c.at("metric").is_string() || !c.contains("wipePosition") || !c.at("wipePosition").is_number())
        return std::unexpected(Context(path, "invalid comparison state"));
      SnapshotComparisonState state;
      state.ReferenceTab = c.at("referenceTab").get<uint32_t>();
      state.CurrentTab = c.at("currentTab").get<uint32_t>();
      state.WipePosition = c.at("wipePosition").get<float>();
      const auto metric = c.at("metric").get<std::string>();
      if (metric == "absoluteError")
        state.Metric = SnapshotComparisonMetric::AbsoluteError;
      else if (metric == "rgbRms")
        state.Metric = SnapshotComparisonMetric::RgbRms;
      else if (metric == "relativeLuminance")
        state.Metric = SnapshotComparisonMetric::RelativeLuminance;
      else
        return std::unexpected(Context(path, "unsupported comparison metric"));
      document.Comparison = state;
    }
    if (auto status = ValidateDocument(document); !status)
      return std::unexpected(Context(path, status.error().Message));
  } catch (const std::exception& exception) {
    return std::unexpected(Context(path, std::string("malformed workspace data: ") + exception.what()));
  }
  return document;
}

} // namespace

auto SnapshotExr::Write(const SnapshotFrame& frame, const std::filesystem::path& path) -> Status {
  if (path.empty() || !ValidExtension(path, ".exr"))
    return std::unexpected(Context(path, "destination must use .exr"));
  const auto pixelCount = CheckedPixels(frame.GetWidth(), frame.GetHeight());
  if (!pixelCount || frame.GetFloatPlanes().size() != Aovs.size() ||
      frame.GetMaterialInstanceIds().size() != *pixelCount || frame.GetOverdraw().size() != *pixelCount)
    return std::unexpected(Context(path, "snapshot frame is incomplete"));
  std::array<OIIO::ImageSpec, 7> specs;
  for (size_t i = 0; i < Aovs.size(); ++i) {
    const auto& plane = frame.GetFloatPlanes()[i];
    const auto& descriptor = RenderOutput::GetDescriptor(Aovs[i]);
    if (plane.Aov != Aovs[i] || plane.ChannelCount != descriptor.ChannelCount ||
        plane.Pixels.size() != *pixelCount * descriptor.ChannelCount)
      return std::unexpected(Context(path, "snapshot float AOV layout is invalid"));
    auto& spec = specs[i];
    spec = OIIO::ImageSpec(static_cast<int>(frame.GetWidth()), static_cast<int>(frame.GetHeight()),
                           static_cast<int>(descriptor.ChannelCount), OIIO::TypeDesc::FLOAT);
    spec.channelnames.clear();
    for (uint32_t c = 0; c < descriptor.ChannelCount; ++c)
      spec.channelnames.emplace_back(descriptor.ChannelNames[c]);
    spec.attribute("oiio:subimagename", descriptor.Name);
    spec.attribute("engine:plane", descriptor.Name);
    spec.attribute("engine:storage", "float32");
    spec.attribute("compression", "zip");
    if (i == 0)
      spec.attribute(std::string(MetadataAttribute), MetadataJson(frame).dump());
  }
  for (size_t i = 5; i < specs.size(); ++i) {
    specs[i] = OIIO::ImageSpec(static_cast<int>(frame.GetWidth()), static_cast<int>(frame.GetHeight()), 1,
                               OIIO::TypeDesc::UINT32);
    const std::string name = i == 5 ? "material_instance_id" : "overdraw";
    specs[i].channelnames = {i == 5 ? "id" : "count"};
    specs[i].attribute("oiio:subimagename", name);
    specs[i].attribute("engine:plane", name);
    specs[i].attribute("engine:storage", "uint32");
    specs[i].attribute("compression", "zip");
  }
  const auto temp = TempPath(path);
  auto output = OIIO::ImageOutput::create("openexr");
  if (!output)
    return std::unexpected(Context(path, "cannot create OpenEXR writer: " + OIIO::geterror()));
  const std::string filename = temp.string();
  if (!output->open(filename, static_cast<int>(specs.size()), specs.data()))
    return std::unexpected(Context(path, "cannot open temporary EXR: " + output->geterror()));
  for (size_t i = 0; i < specs.size(); ++i) {
    if (i > 0 && !output->open(filename, specs[i], OIIO::ImageOutput::AppendSubimage)) {
      const auto error = output->geterror();
      (void)output->close();
      return std::unexpected(Context(path, "cannot append EXR plane: " + error));
    }
    bool written = false;
    if (i < Aovs.size())
      written = output->write_image(
          OIIO::span<const float>(frame.GetFloatPlanes()[i].Pixels.data(), frame.GetFloatPlanes()[i].Pixels.size()));
    else {
      const auto samples = i == 5 ? frame.GetMaterialInstanceIds() : frame.GetOverdraw();
      written = output->write_image(OIIO::span<const uint32_t>(samples.data(), samples.size()));
    }
    if (!written) {
      const auto error = output->geterror();
      (void)output->close();
      std::error_code ignored;
      std::filesystem::remove(temp, ignored);
      return std::unexpected(Context(path, "cannot write EXR plane: " + error));
    }
  }
  if (!output->close())
    return std::unexpected(Context(path, "cannot close EXR: " + output->geterror()));
  if (!AtomicReplace(temp, path)) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return std::unexpected(Context(path, "cannot atomically replace destination"));
  }
  return {};
}

auto SnapshotExr::Read(const std::filesystem::path& path) -> Result<SnapshotFrame> {
  if (path.empty() || !ValidExtension(path, ".exr"))
    return std::unexpected(Context(path, "source must use .exr"));
  auto input = OIIO::ImageInput::open(path.string());
  if (!input)
    return std::unexpected(Context(path, "cannot open EXR: " + OIIO::geterror()));
  SnapshotFrameMetadata metadata;
  std::vector<SnapshotMaterialManifestEntry> manifest;
  std::vector<RenderOutputPlane> floats;
  std::vector<uint32_t> ids, overdraw;
  uint32_t width = 0, height = 0;
  bool parsedMetadata = false;
  for (int part = 0; part < 7; ++part) {
    if (part > 0 && !input->seek_subimage(part, 0)) {
      (void)input->close();
      return std::unexpected(Context(path, "required EXR plane is missing"));
    }
    const OIIO::ImageSpec& spec = input->spec();
    if (spec.width <= 0 || spec.height <= 0 || spec.width != (part == 0 ? spec.width : static_cast<int>(width)) ||
        spec.height != (part == 0 ? spec.height : static_cast<int>(height))) {
      (void)input->close();
      return std::unexpected(Context(path, "EXR planes have inconsistent dimensions"));
    }
    if (part == 0) {
      width = static_cast<uint32_t>(spec.width);
      height = static_cast<uint32_t>(spec.height);
    }
    const auto pixels = CheckedPixels(width, height);
    if (!pixels) {
      (void)input->close();
      return std::unexpected(Context(path, "EXR dimensions are unsupported"));
    }
    const auto* nameAttr = spec.find_attribute("engine:plane", OIIO::TypeDesc::STRING);
    if (nameAttr == nullptr) {
      (void)input->close();
      return std::unexpected(Context(path, "EXR plane is missing its semantic name"));
    }
    const std::string name = nameAttr->get_string();
    const auto* compression = spec.find_attribute("compression", OIIO::TypeDesc::STRING);
    if (compression == nullptr || compression->get_string() != "zip") {
      (void)input->close();
      return std::unexpected(Context(path, "EXR plane uses an unsupported or lossy encoding"));
    }
    if (part < 5) {
      const auto& descriptor = RenderOutput::GetDescriptor(Aovs[part]);
      if (name != descriptor.Name || spec.format != OIIO::TypeDesc::FLOAT ||
          spec.nchannels != static_cast<int>(descriptor.ChannelCount) ||
          spec.channelnames.size() != descriptor.ChannelCount) {
        (void)input->close();
        return std::unexpected(Context(path, "EXR float AOV has unsupported name, format, or channels"));
      }
      for (uint32_t c = 0; c < descriptor.ChannelCount; ++c)
        if (spec.channelnames[c] != descriptor.ChannelNames[c]) {
          (void)input->close();
          return std::unexpected(Context(path, "EXR channel layout does not match the AOV contract"));
        }
      RenderOutputPlane plane{Aovs[part], descriptor.ChannelCount,
                              std::vector<float>(*pixels * descriptor.ChannelCount)};
      if (!input->read_image(part, 0, 0, spec.nchannels, OIIO::span<float>(plane.Pixels.data(), plane.Pixels.size()))) {
        const auto error = input->geterror();
        (void)input->close();
        return std::unexpected(Context(path, "cannot read EXR float plane: " + error));
      }
      floats.push_back(std::move(plane));
      if (part == 0) {
        const auto* attr = spec.find_attribute(std::string(MetadataAttribute), OIIO::TypeDesc::STRING);
        if (!attr) {
          (void)input->close();
          return std::unexpected(Context(path, "EXR capture metadata is missing"));
        }
        try {
          parsedMetadata = ParseMetadata(Json::parse(attr->get_string()), metadata, manifest);
        } catch (const std::exception&) {
          parsedMetadata = false;
        }
        if (!parsedMetadata) {
          (void)input->close();
          return std::unexpected(Context(path, "EXR capture metadata is malformed or unsupported"));
        }
      }
    } else {
      const std::string expected = part == 5 ? "material_instance_id" : "overdraw";
      if (name != expected || spec.format != OIIO::TypeDesc::UINT32 || spec.nchannels != 1 ||
          spec.channelnames.size() != 1 || spec.channelnames[0] != (part == 5 ? "id" : "count")) {
        (void)input->close();
        return std::unexpected(Context(path, "EXR integer plane must be exact single-channel UINT32"));
      }
      auto& samples = part == 5 ? ids : overdraw;
      samples.resize(*pixels);
      if (!input->read_image(part, 0, 0, spec.nchannels, OIIO::span<uint32_t>(samples.data(), samples.size()))) {
        const auto error = input->geterror();
        (void)input->close();
        return std::unexpected(Context(path, "cannot read EXR integer plane: " + error));
      }
    }
  }
  if (input->seek_subimage(7, 0)) {
    (void)input->close();
    return std::unexpected(Context(path, "EXR contains unsupported extra planes"));
  }
  (void)input->close();
  if (metadata.OutputWidth != width || metadata.OutputHeight != height)
    return std::unexpected(Context(path, "EXR metadata dimensions do not match its channels"));
  auto frame = SnapshotFrame::Create(std::move(metadata), std::move(manifest), width, height, std::move(floats),
                                     std::move(ids), std::move(overdraw));
  if (!frame)
    return std::unexpected(Context(path, frame.error().Message));
  return std::move(*frame);
}

auto SnapshotWorkspaceFile::Save(const std::filesystem::path& path, const SnapshotWorkspaceDocument& document)
    -> Status {
  if (path.empty() || !ValidExtension(path, ".aframe"))
    return std::unexpected(Context(path, "destination must use .aframe"));
  if (auto status = ValidateDocument(document); !status)
    return std::unexpected(Context(path, status.error().Message));
  for (const auto& tab : document.Tabs) {
    auto resolved = tab.ExrPath.is_absolute() ? tab.ExrPath : path.parent_path() / tab.ExrPath;
    std::error_code existsError;
    const bool exists = std::filesystem::exists(resolved, existsError);
    if (existsError || !exists)
      return std::unexpected(Context(path, "referenced EXR is missing: " + resolved.generic_string()));
  }
  const auto temporary = TempPath(path);
  try {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
      return std::unexpected(Context(path, "cannot open temporary workspace file"));
    output << WorkspaceJson(document, path).dump(2) << '\n';
    output.flush();
    if (!output) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return std::unexpected(Context(path, "cannot write workspace JSON"));
    }
  } catch (const std::exception& exception) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return std::unexpected(Context(path, std::string("cannot serialize workspace: ") + exception.what()));
  }
  if (!AtomicReplace(temporary, path)) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return std::unexpected(Context(path, "cannot atomically replace workspace"));
  }
  return {};
}

auto SnapshotWorkspaceFile::Load(const std::filesystem::path& path) -> Result<SnapshotLoadedWorkspace> {
  if (path.empty() || !ValidExtension(path, ".aframe"))
    return std::unexpected(Context(path, "source must use .aframe"));
  Json json;
  try {
    std::ifstream input(path, std::ios::binary);
    if (!input)
      return std::unexpected(Context(path, "cannot open workspace"));
    input >> json;
  } catch (const std::exception& exception) {
    return std::unexpected(Context(path, std::string("cannot parse workspace JSON: ") + exception.what()));
  }
  auto parsed = ReadWorkspaceJson(json, path);
  if (!parsed)
    return std::unexpected(parsed.error());
  SnapshotLoadedWorkspace loaded;
  loaded.Document = std::move(*parsed);
  loaded.Frames.reserve(loaded.Document.Tabs.size());
  loaded.ResolvedExrPaths.reserve(loaded.Document.Tabs.size());
  for (const auto& tab : loaded.Document.Tabs) {
    const auto resolved = tab.ExrPath.is_absolute() ? tab.ExrPath : path.parent_path() / tab.ExrPath;
    auto frame = SnapshotExr::Read(resolved);
    if (!frame)
      return std::unexpected(Context(path, frame.error().Message));
    if (tab.PinnedPixel && (tab.PinnedPixel->X >= frame->GetWidth() || tab.PinnedPixel->Y >= frame->GetHeight()))
      return std::unexpected(Context(path, "pinned pixel lies outside EXR payload: " + resolved.generic_string()));
    if (tab.Roi && (static_cast<uint64_t>(tab.Roi->X) + tab.Roi->Width > frame->GetWidth() ||
                    static_cast<uint64_t>(tab.Roi->Y) + tab.Roi->Height > frame->GetHeight()))
      return std::unexpected(Context(path, "ROI lies outside EXR payload: " + resolved.generic_string()));
    std::error_code absoluteError;
    auto absolutePath = std::filesystem::absolute(resolved, absoluteError);
    if (absoluteError)
      return std::unexpected(Context(path, "cannot resolve EXR path: " + resolved.generic_string()));
    loaded.ResolvedExrPaths.push_back(std::move(absolutePath));
    loaded.Frames.push_back(std::move(*frame));
  }
  return loaded;
}

} // namespace Engine::SnapshotPersistence
