#pragma once
#include "Application.h"
#include "renderer/Mesh.h"
#include "renderer/Material.h"
#include "renderer/Entity.h"
#include "renderer/Camera.h"
#include <memory>
#include <vector>

class SandboxApp : public Engine::Application {
public:
	SandboxApp() = default;
	~SandboxApp() override = default;

protected:
	void OnInit() override;
	void OnRender() override;
	void OnUpdate(float deltaTime) override;
	void OnUIRender() override;

private:
	std::vector<std::unique_ptr<Renderer::Entity>> m_Cubes;
	std::vector<DirectX::XMFLOAT3> m_RotationSpeeds;
	std::vector<DirectX::XMFLOAT4> m_CubeColors;
	float m_Timer = 0.0f;

	std::vector<std::shared_ptr<Renderer::Camera>> cameras;
};