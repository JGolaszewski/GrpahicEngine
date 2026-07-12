#include "RenderGraph.h"

namespace Renderer {
	RenderGraph::RenderGraph(nvrhi::DeviceHandle device)
		: m_Device(device), m_Pool(device) {
	}

	void RenderGraph::Reset() {
		m_Registry.Clear();
		m_Passes.clear();
	}

	void RenderGraph::AddPass(std::unique_ptr<IRenderPass> pass) {
		RGPassDependencies deps;
		RGBuilder builder(m_Registry, deps);

		pass->Setup(builder);

		m_Passes.push_back(std::move(pass));
	}

	void RenderGraph::Compile() {
		for (auto& resource : m_Registry.GetAllResources()) {
			if (!resource.isImported) {
				resource.physicalHandle = m_Pool.Acquire(resource.desc);
			}
		}

		// TODO: Add aliasing and First / Last use calculation
	}

	void RenderGraph::Execute(nvrhi::ICommandList* cmd) {
		RenderContext ctx(&m_Registry);

		for (auto& pass : m_Passes) {
			pass->Execute(cmd, ctx);
		}

		for (auto& resource : m_Registry.GetAllResources()) {
			if (!resource.isImported && resource.physicalHandle) {
				m_Pool.Release(resource.desc, resource.physicalHandle);
			}
		}
	}
}