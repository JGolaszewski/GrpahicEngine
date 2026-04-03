#include "SceneRenderer.h"

void Engine::SceneRenderer::Init(nvrhi::IDevice* device) {
	m_PipelineCache.Init(device);

	nvrhi::BufferDesc cbDesc;
	cbDesc.byteSize = sizeof(DirectX::XMMATRIX);
	cbDesc.isConstantBuffer = true;
	cbDesc.isVolatile = true;
	cbDesc.debugName = "PerObject_MVP_Buffer";
	m_PerObjectBuffer = device->createBuffer(cbDesc);

	nvrhi::BindingLayoutDesc layoutDesc;
	layoutDesc.visibility = nvrhi::ShaderType::All;
	layoutDesc.bindings = {
		nvrhi::BindingLayoutItem::ConstantBuffer(0)
	};
	m_PerObjectBindingLayout = device->createBindingLayout(layoutDesc);

	nvrhi::BindingSetDesc setDesc;
	setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_PerObjectBuffer));
	m_PerObjectBindingSet = device->createBindingSet(setDesc, m_PerObjectBindingLayout);
}

void Engine::SceneRenderer::Submit(Renderer::Entity* entity) {
	if(entity && entity->mesh && entity->material) {
		m_CommandQueue.push_back(entity);
	}
}

void Engine::SceneRenderer::RenderAll(nvrhi::ICommandList* cmd, nvrhi::IFramebuffer* fb, const DirectX::XMMATRIX& viewProj) {
	const nvrhi::FramebufferInfoEx& fbInfo = fb->getFramebufferInfo();
	nvrhi::Viewport viewport = nvrhi::Viewport(float(fbInfo.width), float(fbInfo.height));
	nvrhi::Rect scissor = nvrhi::Rect(fbInfo.width, fbInfo.height);

	for(auto* entity : m_CommandQueue) {
		DirectX::XMMATRIX model = entity->transform.GetWorldMat();
		DirectX::XMMATRIX mvp = model * viewProj;
		DirectX::XMMATRIX mvpTransposed = DirectX::XMMatrixTranspose(mvp);

		cmd->writeBuffer(m_PerObjectBuffer, &mvpTransposed, sizeof(DirectX::XMMATRIX));

		Engine::PipelineKey key;
		key.VS = entity->material->GetVS();
		key.PS = entity->material->GetPS();
		key.GlobalLayout = m_PerObjectBindingLayout;
		key.MaterialLayout = entity->material->GetBindingLayout();

		nvrhi::GraphicsState state;

		state.pipeline = m_PipelineCache.GetOrCreatePipeline(key, fb);


		state.framebuffer = fb;

		state.viewport.addViewport(viewport);
		state.viewport.addScissorRect(scissor);

		state.bindings = {
			m_PerObjectBindingSet,
			entity->material->GetBindingSet()
		};

		nvrhi::VertexBufferBinding vbb;
		vbb.buffer = entity->mesh->GetVertexBuffer();
		vbb.slot = 0;
		vbb.offset = 0;
		state.vertexBuffers = { vbb };

		if(entity->mesh->HasIndices()) {
			state.indexBuffer.buffer = entity->mesh->GetIndexBuffer();
			state.indexBuffer.format = nvrhi::Format::R32_UINT;
			state.indexBuffer.offset = 0;
		}

		cmd->setGraphicsState(state);

		nvrhi::DrawArguments args;
		args.instanceCount = 1;

		if(entity->mesh->HasIndices()) {
			args.vertexCount = entity->mesh->GetIndexCount();
			cmd->drawIndexed(args);
		} else {
			args.vertexCount = entity->mesh->GetVertexCount();
			cmd->draw(args);
		}
	}

	m_CommandQueue.clear();
}

nvrhi::BindingLayoutHandle Engine::SceneRenderer::GetPerObjectBindingLayout() const {
	return m_PerObjectBindingLayout;
}
