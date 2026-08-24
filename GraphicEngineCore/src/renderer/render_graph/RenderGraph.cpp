#include "renderer/render_graph/RenderGraph.h"

#include <queue>
#include <unordered_map>

namespace Renderer {
	RenderGraph::RenderGraph(nvrhi::DeviceHandle device)
		: m_Device(device), m_Pool(device) {
	}

	void RenderGraph::Reset() {
		m_Registry.Clear();
		m_Nodes.clear();
		m_Blackboard.Clear();
	}

	void RenderGraph::AddPass(std::unique_ptr<IRenderPass> pass) {
		RGPassDependencies deps;
		RGBuilder builder(m_Registry, deps);

		pass->Setup(builder, m_Blackboard);

		RGPassNode node;

		node.pass = std::move(pass);
		node.deps = std::move(deps);

		m_Nodes.push_back(std::move(node));
	}

	void RenderGraph::Compile() {
		// Topological sort (Kahn's algorithm)
		size_t n = m_Nodes.size();
		std::vector<int> inDegree(n, 0);
		std::vector<std::vector<size_t>> adj(n);

		for (size_t i = 0; i < n; ++i) {
			for (size_t j = 0; j < n; ++j) {
				if (i == j) continue;
				for (const auto& out : m_Nodes[i].deps.outputs) {
					for (const auto& in : m_Nodes[j].deps.inputs) {
						if (out.value == in.value) {
							adj[i].push_back(j);
							inDegree[j]++;
						}
					}
				}
			}
		}

		std::queue<size_t> q;
		for (size_t i = 0; i < n; ++i) {
			if (inDegree[i] == 0) q.push(i);
		}

		std::vector<size_t> sortedIndices;
		sortedIndices.reserve(n);

		while (!q.empty()) {
			size_t u = q.front();
			q.pop();

			sortedIndices.push_back(u);

			for (size_t v : adj[u]) {
				if (--inDegree[v] == 0) q.push(v);
			}
		}

		if (sortedIndices.size() == n) {
			std::vector<RGPassNode> sortedNodes;
			sortedNodes.reserve(n);

			for (size_t idx : sortedIndices) {
				sortedNodes.push_back(std::move(m_Nodes[idx]));
			}
			m_Nodes = std::move(sortedNodes);
		}
		else {
			assert(false); //TODO: CHANGE !
		}

		for (auto& resource : m_Registry.GetAllResources()) {
			if (!resource.isImported) {
				resource.physicalHandle = m_Pool.Acquire(resource.desc);
			}
		}

		// TODO: Add aliasing and First / Last use calculation
	}

	void RenderGraph::Execute(nvrhi::ICommandList* cmd) {
		float deltaTime = m_Blackboard.Get<float>("DeltaTime", 0.016f);

		RenderContext ctx(m_Device.Get(), &m_Registry, &m_Blackboard, deltaTime);

		for (auto& node : m_Nodes) {
			node.pass->Execute(cmd, ctx);
		}

		for (auto& resource : m_Registry.GetAllResources()) {
			if (!resource.isImported && resource.physicalHandle) {
				m_Pool.Release(resource.desc, resource.physicalHandle);
			}
		}
	}
}
