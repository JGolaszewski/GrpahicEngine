#include "rendering/include/renderer/brdf/Brdf.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Camera.h"
#include "rendering/include/renderer/Viewport.h"
#include "rendering/include/renderer/render_graph/RenderView.h"
#include "scene/include/components/Transform.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace {
	void Require(bool condition, const char* message) {
		if (!condition) {
			std::cerr << message << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	float IntegrateNdf(float alpha, float gamma) {
		constexpr uint32_t sampleCount = 20000;
		const float step = DirectX::XM_PIDIV2 / static_cast<float>(sampleCount);
		float integral = 0.0f;
		for (uint32_t i = 0; i < sampleCount; ++i) {
			const float theta = (static_cast<float>(i) + 0.5f) * step;
			const float cosine = std::cos(theta);
			const float sine = std::sin(theta);
			integral += ::Engine::Renderer::Brdf::EvaluateGtrNdf(cosine, alpha, gamma) * cosine * sine * step;
		}
		return 2.0f * ::Engine::Renderer::Brdf::Pi * integral;
	}

	float IntegrateWhiteFurnace(const DirectX::XMFLOAT3& view, float alpha, const DirectX::XMFLOAT3& diffuse, const DirectX::XMFLOAT3& f0) {
		constexpr uint32_t thetaCount = 160;
		constexpr uint32_t phiCount = 320;
		const float thetaStep = DirectX::XM_PIDIV2 / static_cast<float>(thetaCount);
		const float phiStep = 2.0f * ::Engine::Renderer::Brdf::Pi / static_cast<float>(phiCount);
		float reflectedEnergy = 0.0f;
		for (uint32_t thetaIndex = 0; thetaIndex < thetaCount; ++thetaIndex) {
			const float theta = (static_cast<float>(thetaIndex) + 0.5f) * thetaStep;
			const float sinTheta = std::sin(theta);
			const float cosTheta = std::cos(theta);
			for (uint32_t phiIndex = 0; phiIndex < phiCount; ++phiIndex) {
				const float phi = (static_cast<float>(phiIndex) + 0.5f) * phiStep;
				const DirectX::XMFLOAT3 light = {
				sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };
				const DirectX::XMFLOAT3 value = ::Engine::Renderer::Brdf::EvaluateGgxBrdf(
					{ 0.0f, 0.0f, 1.0f }, view, light, diffuse, f0, alpha);
				reflectedEnergy += (value.x + value.y + value.z) / 3.0f * cosTheta * sinTheta * thetaStep * phiStep;
			}
		}
		return reflectedEnergy;
	}

	void TestProjectedNdfNormalization() {
		for (float gamma : { 1.0f, 1.5f, 2.0f, 4.0f, 8.0f }) {
			for (float alpha : { 0.05f, 0.2f, 0.65f, 1.0f }) {
				const float integral = IntegrateNdf(alpha, gamma);
				Require(std::abs(integral - 1.0f) < 2.0e-4f, "GTR projected NDF must normalize to one");
			}
		}
	}

	void TestGtrLimitsAndRoughnessMapping() {
		for (float noH : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
			Require(std::abs(::Engine::Renderer::Brdf::EvaluateGtrNdf(noH, 0.4f, 2.0f) -
				::Engine::Renderer::Brdf::EvaluateGgxNdf(noH, 0.4f)) < 1.0e-6f,
				"GTR gamma=2 must be the GGX/Trowbridge-Reitz distribution");
			Require(std::abs(::Engine::Renderer::Brdf::EvaluateGtrNdf(noH, 1.0f, 1.0f) - 1.0f / ::Engine::Renderer::Brdf::Pi) < 1.0e-6f,
				"GTR alpha=1 must be uniform for gamma=1");
		}
		const float alpha = 0.35f;
		Require(std::abs(::Engine::Renderer::Brdf::EvaluateGtrNdf(1.0f, alpha, 1.0f) -
			::Engine::Renderer::Brdf::GtrNormalization(alpha, 1.0f) / (alpha * alpha)) < 1.0e-6f,
			"GTR gamma=1 limit must use the logarithmic normalization");
		Require(std::abs(::Engine::Renderer::Brdf::AlphaFromPerceptualRoughness(0.5f) - 0.25f) < 1.0e-6f,
			"Perceptual roughness must map to squared slope roughness");
	}

	void TestReciprocityAndWhiteFurnace() {
		const DirectX::XMFLOAT3 normal = { 0.0f, 0.0f, 1.0f };
		const DirectX::XMFLOAT3 view = { 0.3f, 0.0f, 0.9539392f };
		const DirectX::XMFLOAT3 light = { -0.1f, 0.5f, 0.8602325f };
		const DirectX::XMFLOAT3 diffuse = { 0.7f, 0.4f, 0.2f };
		const DirectX::XMFLOAT3 f0 = { 0.04f, 0.04f, 0.04f };
		const auto forward = ::Engine::Renderer::Brdf::EvaluateGgxBrdf(normal, view, light, diffuse, f0, 0.35f);
		const auto reverse = ::Engine::Renderer::Brdf::EvaluateGgxBrdf(normal, light, view, diffuse, f0, 0.35f);
		Require(std::abs(forward.x - reverse.x) < 1.0e-6f &&
			std::abs(forward.y - reverse.y) < 1.0e-6f &&
			std::abs(forward.z - reverse.z) < 1.0e-6f,
			"GGX BRDF must satisfy reciprocity");

		for (float alpha : { 0.05f, 0.2f, 0.5f, 1.0f }) {
			for (const auto& material : {
				std::pair{ DirectX::XMFLOAT3{ 0.7f, 0.4f, 0.2f }, DirectX::XMFLOAT3{ 0.04f, 0.04f, 0.04f } },
				std::pair{ DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f }, DirectX::XMFLOAT3{ 0.9f, 0.7f, 0.4f } } }) {
				const float energy = IntegrateWhiteFurnace(view, alpha, material.first, material.second);
				Require(energy >= 0.0f && energy <= 1.001f, "Single-scatter GGX material must obey white-furnace energy bounds");
			}
		}
	}

	void TestMaterialGpuAbiPacking() {
		::Engine::Renderer::MaterialProperties properties;
		properties.Model = ::Engine::Renderer::BrdfModel::GTR;
		properties.SpecularMode = ::Engine::Renderer::DielectricSpecularMode::ExplicitF0;
		properties.Roughness = 0.5f;
		properties.GtrGamma = 3.0f;
		properties.Ior = 1.7f;
		properties.SpecularF0 = { 0.2f, 0.3f, 0.4f };
		const ::Engine::Renderer::MaterialGpu gpu = ::Engine::Renderer::BuildMaterialGpu(properties);

		Require(gpu.ModelFlags.x == ::Engine::Renderer::MaterialGpuAbiVersion, "Material GPU ABI version must be packed in the flags slot");
		Require(gpu.ModelFlags.y == static_cast<uint32_t>(::Engine::Renderer::BrdfModel::GTR), "Material model selector must be packed");
		Require(gpu.ModelFlags.z == static_cast<uint32_t>(::Engine::Renderer::DielectricSpecularMode::ExplicitF0), "Specular policy must be packed");
		Require(std::abs(gpu.SurfaceParams.x - 0.5f) < 1.0e-6f && std::abs(gpu.SurfaceParams.y - 0.25f) < 1.0e-6f,
			"Perceptual and slope roughness must be packed together");
		Require(std::abs(gpu.SurfaceParams.z - 3.0f) < 1.0e-6f && std::abs(gpu.SurfaceParams.w - 1.7f) < 1.0e-6f,
			"GTR exponent and index of refraction must be packed");
		Require(std::abs(gpu.SpecularParams.x - 0.2f) < 1.0e-6f &&
			std::abs(gpu.SpecularParams.y - 0.3f) < 1.0e-6f &&
			std::abs(gpu.SpecularParams.z - 0.4f) < 1.0e-6f,
			"Explicit dielectric F0 must be packed");

		properties.Roughness = -1.0f;
		properties.Metallic = 2.0f;
		properties.GtrGamma = 16.0f;
		properties.Ior = 0.0f;
		const ::Engine::Renderer::MaterialGpu clamped = ::Engine::Renderer::BuildMaterialGpu(properties);
		Require(clamped.SurfaceParams.x == 0.0f && clamped.SurfaceParams.y == ::Engine::Renderer::MaterialMinAlpha &&
			clamped.SurfaceParams.z == ::Engine::Renderer::MaterialMaxGtrGamma &&
			clamped.SurfaceParams.w == ::Engine::Renderer::MaterialMinIor &&
			clamped.EmissiveMetallic.w == 1.0f,
			"Material GPU packing must clamp model parameters to supported ranges");
	}

	void TestMaterialPropertiesRoundTrip() {
		::Engine::Renderer::Material material(0);
		::Engine::Renderer::MaterialProperties properties;
		properties.Model = ::Engine::Renderer::BrdfModel::GTR;
		properties.SpecularMode = ::Engine::Renderer::DielectricSpecularMode::ExplicitF0;
		properties.GtrGamma = 4.0f;
		properties.Ior = 1.8f;
		properties.SpecularF0 = { 0.12f, 0.23f, 0.34f };
		material.SetProperties(properties);
		const auto restored = material.GetProperties();
		Require(restored.Model == properties.Model && restored.SpecularMode == properties.SpecularMode &&
			std::abs(restored.GtrGamma - properties.GtrGamma) < 1.0e-6f &&
			std::abs(restored.Ior - properties.Ior) < 1.0e-6f &&
			std::abs(restored.SpecularF0.z - properties.SpecularF0.z) < 1.0e-6f,
			"Material must retain authored BRDF model parameters");
	}

	void TestRenderViewCameraWorldPosition() {
		::Engine::Renderer::Camera camera(0.7853982f, 0.1f, 100.0f);
		camera.m_Transform.SetPosition(3.0f, 4.0f, 5.0f);
		Engine::Viewport viewport(320, 200, nullptr);
		::Engine::Renderer::RenderView view;
		view.Initialize(&camera, &viewport);
		Require(std::abs(view.CameraPosition.x - 3.0f) < 1.0e-6f &&
			std::abs(view.CameraPosition.y - 4.0f) < 1.0e-6f &&
			std::abs(view.CameraPosition.z - 5.0f) < 1.0e-6f,
			"Render views must retain camera world position for the GPU camera buffer");
	}

	void TestNormalTransformPreservesPerpendicularity() {
		::Engine::Scene::Components::Transform transform;
		transform.SetScale(2.0f, 1.0f, 0.5f);
		transform.SetRotation(0.3f, -0.7f, 0.2f);
		const DirectX::XMVECTOR tangent = DirectX::XMVectorSet(1.0f, 0.0f, -1.0f, 0.0f);
		const DirectX::XMVECTOR normal = DirectX::XMVectorSet(1.0f, 0.0f, 1.0f, 0.0f);
		const DirectX::XMVECTOR worldTangent = DirectX::XMVector3TransformNormal(tangent, transform.GetWorldMat());
		const DirectX::XMVECTOR worldNormal = DirectX::XMVector3TransformNormal(normal, transform.GetNormalMat());
		const float dot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(worldTangent, worldNormal));
		Require(std::isfinite(dot) && std::abs(dot) < 1.0e-5f,
			"Inverse-transpose normal transforms must preserve tangent orthogonality under nonuniform scale");

		transform.SetScale(0.0f, 1.0f, 1.0f);
		const DirectX::XMVECTOR singularNormal = DirectX::XMVector3TransformNormal(normal, transform.GetNormalMat());
		Require(DirectX::XMVector3IsNaN(singularNormal) == false &&
			DirectX::XMVector3IsInfinite(singularNormal) == false,
			"Singular transforms must return a finite normal fallback");
	}
}

int main() {
	TestProjectedNdfNormalization();
	TestGtrLimitsAndRoughnessMapping();
	TestReciprocityAndWhiteFurnace();
	TestMaterialGpuAbiPacking();
	TestMaterialPropertiesRoundTrip();
	TestRenderViewCameraWorldPosition();
	TestNormalTransformPreservesPerpendicularity();
	std::cout << "BRDF reference tests passed\n";
	return EXIT_SUCCESS;
}
