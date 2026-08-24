#pragma once
#include <string>
#include <unordered_map>
#include <any>
#include <vector>
#include <nvrhi/nvrhi.h>
#include "renderer/Light.h"

namespace Renderer { class RenderView; }

namespace Renderer {

	class FrameBlackboard {
	public:
		template<typename T>
		void Set(const std::string& key, T value) {
			m_Data[key] = std::move(value);
		}

		template<typename T>
		T& Get(const std::string& key) {
			return std::any_cast<T&>(m_Data.at(key));
		}

		template<typename T>
		T Get(const std::string& key, T defaultValue) const {
			auto it = m_Data.find(key);
			if (it != m_Data.end()) {
				return std::any_cast<T>(it->second);
			}
			return defaultValue;
		}

		void Clear() {
			m_Data.clear();
		}

		void SetSceneViews(const std::vector<RenderView*>& views) { Set("SceneViews", views); }
		std::vector<RenderView*> GetSceneViews() { return Get<std::vector<RenderView*>>("SceneViews", {}); }

		void SetDirectionalLights(const std::vector<DirectionalLightData>& lights) { Set("DirectionalLights", lights); }
		std::vector<DirectionalLightData>& GetDirectionalLights() { return Get<std::vector<DirectionalLightData>>("DirectionalLights"); }

		void SetLightBuffer(nvrhi::BufferHandle buffer) { Set("LightBuffer", buffer); }
		nvrhi::BufferHandle GetLightBuffer() { return Get<nvrhi::BufferHandle>("LightBuffer"); }

		void SetShadowSampler(nvrhi::SamplerHandle sampler) { Set("ShadowSampler", sampler); }
		nvrhi::SamplerHandle GetShadowSampler() { return Get<nvrhi::SamplerHandle>("ShadowSampler"); }

		void SetPerObjectBindingLayout(nvrhi::BindingLayoutHandle layout) { Set("PerObjectBindingLayout", layout); }
		nvrhi::BindingLayoutHandle GetPerObjectBindingLayout() { return Get<nvrhi::BindingLayoutHandle>("PerObjectBindingLayout"); }

		void SetSceneBindingLayout(nvrhi::BindingLayoutHandle layout) { Set("SceneBindingLayout", layout); }
		nvrhi::BindingLayoutHandle GetSceneBindingLayout() { return Get<nvrhi::BindingLayoutHandle>("SceneBindingLayout"); }
		
		void SetShadowViews(const std::vector<RenderView*>& views) { Set("ShadowViews", views); }
		std::vector<RenderView*> GetShadowViews() { return Get<std::vector<RenderView*>>("ShadowViews", {}); }
	private:
		std::unordered_map<std::string, std::any> m_Data;
	};
}