#include "SandboxApp.h"
#include "UIWin.h"

#include <imgui.h>
#include <random>


void SandboxApp::OnInit() {
	ImGui::SetCurrentContext(GetImGuiContext());

	auto* device = GetRenderer()->GetDevice();
	auto* factory = GetShaderFactory();

	auto vs = factory->GetShader("shaders/color_vs.dxbc", nvrhi::ShaderType::Vertex);
	auto ps = factory->GetShader("shaders/color_ps.dxbc", nvrhi::ShaderType::Pixel);

	auto* fb = GetRenderer()->GetDefaultFramebuffer();
	float width = (float)fb->getFramebufferInfo().width;
	float height = (float)fb->getFramebufferInfo().height;

	float aspect = width / (height > 0.0f ? height : 1.0f);

	auto main_camera = std::make_shared<Renderer::Camera>(DirectX::XM_PIDIV4, aspect, 0.1f, 100.0f);
	main_camera->m_Transform.SetPosition(0, 0, -5);

	SetActiveCamera(main_camera);

	auto* cmd = GetCommandList();
	
	cmd->open();

	std::random_device rd;
	std::mt19937 gen(rd());

	std::uniform_real_distribution<float> disPosX(-20.0f, 20.0f);
	std::uniform_real_distribution<float> disPosY(-20.0f, 20.0f);
	std::uniform_real_distribution<float> disPosZ(5.0f, 50.0f);
	std::uniform_real_distribution<float> disScale(0.2f, 3.0f);

	std::uniform_real_distribution<float> disColor(0.0f, 1.0f);
	std::uniform_real_distribution<float> disSpeed(-2.0f, 2.0f);

	for(int i = 0; i < 10000; i++) {
		float randomScale = disScale(gen);

		DirectX::XMFLOAT4 color = { disColor(gen), disColor(gen), disColor(gen), 1.0f };
		m_CubeColors.push_back(color);

		DirectX::XMFLOAT3 speed = { disSpeed(gen), disSpeed(gen), disSpeed(gen) };
		m_RotationSpeeds.push_back(speed);

		m_Cubes.push_back(
			Renderer::EntityBuilder(device, cmd)
			.Cube()
			.Material(vs, ps)
			.Position(disPosX(gen), disPosY(gen), disPosZ(gen))
			.Scale(randomScale, randomScale, randomScale)
			.Color(color.x, color.y, color.z, color.w)
			.Build()
		);
	}
	cmd->close();
	device->executeCommandList(cmd);
}

void SandboxApp::OnUpdate(float deltaTime) {
	m_Timer += deltaTime;

	for(size_t i = 0; i < m_Cubes.size(); i++) {
		float speedX = m_RotationSpeeds[i].x;
		float speedY = m_RotationSpeeds[i].y;
		float speedZ = m_RotationSpeeds[i].z;

		m_Cubes[i]->transform.Rotate(
			speedX * deltaTime,
			speedY * deltaTime,
			speedZ * deltaTime
		);
	}
}

void SandboxApp::OnRender() {
	auto* commandList = GetCommandList();

	for(size_t i = 0; i < m_Cubes.size(); i++) {
		m_Cubes[i]->material->SetColor(m_CubeColors[i], commandList);

		GetSceneRenderer()->Submit(m_Cubes[i].get());
	}
}

void SandboxApp::OnUIRender() {
	statWindow();
	cameraWindow(&GetActiveCamera()->m_Transform);
}