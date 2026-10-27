#pragma once

#include "assets/include/AssetImporter.h"
#include <nvrhi/nvrhi.h>
#include <string>
#include <vector>

namespace Engine::Renderer {

/// GPU resources for the renderer's built-in Studio environment.
class EnvironmentLightingResources {
public:
  bool Initialize(nvrhi::IDevice* device, std::string& error);
  bool Initialize(nvrhi::IDevice* device, const ::Engine::ImportedEnvironment& source, std::string& error);

  nvrhi::TextureHandle Panorama;
  nvrhi::TextureHandle IrradianceMap;
  nvrhi::TextureHandle PrefilteredEnvironment;
  nvrhi::TextureHandle BrdfIntegrationLut;
  nvrhi::SamplerHandle PanoramaSampler;
  nvrhi::SamplerHandle IrradianceSampler;
  nvrhi::SamplerHandle PrefilteredEnvironmentSampler;
  nvrhi::SamplerHandle BrdfIntegrationSampler;
};

} // namespace Engine::Renderer
