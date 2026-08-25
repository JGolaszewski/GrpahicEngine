#pragma once

#include <any>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <nvrhi/nvrhi.h>

#include "renderer/Light.h"
#include "renderer/render_graph/RGResources.h"

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
			m_SceneViews.clear();
			m_ShadowViews.clear();
			m_DirectionalLights.clear();
			m_ShadowMap = {};
			m_LightBuffer = nullptr;
			m_ShadowSampler = nullptr;
			m_PerObjectBindingLayout = nullptr;
			m_SceneBindingLayout = nullptr;
		}

		void SetSceneViews(std::vector<RenderView*> views) { m_SceneViews = std::move(views); }
		const std::vector<RenderView*>& GetSceneViews() const { return m_SceneViews; }

		void SetDirectionalLights(std::vector<DirectionalLightData> lights) { m_DirectionalLights = std::move(lights); }
		const std::vector<DirectionalLightData>& GetDirectionalLights() const { return m_DirectionalLights; }

		void SetLightBuffer(nvrhi::BufferHandle buffer) { m_LightBuffer = buffer; }
		nvrhi::BufferHandle GetLightBuffer() const { return m_LightBuffer; }

		void SetShadowSampler(nvrhi::SamplerHandle sampler) { m_ShadowSampler = sampler; }
		nvrhi::SamplerHandle GetShadowSampler() const { return m_ShadowSampler; }

		void SetPerObjectBindingLayout(nvrhi::BindingLayoutHandle layout) { m_PerObjectBindingLayout = layout; }
		nvrhi::BindingLayoutHandle GetPerObjectBindingLayout() const { return m_PerObjectBindingLayout; }

		void SetSceneBindingLayout(nvrhi::BindingLayoutHandle layout) { m_SceneBindingLayout = layout; }
		nvrhi::BindingLayoutHandle GetSceneBindingLayout() const { return m_SceneBindingLayout; }

		void SetShadowViews(std::vector<RenderView*> views) { m_ShadowViews = std::move(views); }
		const std::vector<RenderView*>& GetShadowViews() const { return m_ShadowViews; }

		void SetShadowMap(RGResourceID id) { m_ShadowMap = id; }
		RGResourceID GetShadowMap() const { return m_ShadowMap; }

	private:
		std::unordered_map<std::string, std::any> m_Data;
		std::vector<RenderView*> m_SceneViews;
		std::vector<RenderView*> m_ShadowViews;
		std::vector<DirectionalLightData> m_DirectionalLights;
		RGResourceID m_ShadowMap;
		nvrhi::BufferHandle m_LightBuffer;
		nvrhi::SamplerHandle m_ShadowSampler;
		nvrhi::BindingLayoutHandle m_PerObjectBindingLayout;
		nvrhi::BindingLayoutHandle m_SceneBindingLayout;
	};
}
