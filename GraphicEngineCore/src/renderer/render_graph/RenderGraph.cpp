#include "renderer/render_graph/RenderGraph.h"

#include <algorithm>
#include <iostream>
#include <queue>
#include <sstream>
#include <vector>

namespace {
	bool Contains(const std::vector<Renderer::RGResourceID>& ids, Renderer::RGResourceID id) {
		return std::find(ids.begin(), ids.end(), id) != ids.end();
	}

	void AddEdge(std::vector<std::vector<size_t>>& adjacency, std::vector<int>& inDegree, size_t from, size_t to) {
		if (from == to || std::find(adjacency[from].begin(), adjacency[from].end(), to) != adjacency[from].end()) {
			return;
		}
		adjacency[from].push_back(to);
		++inDegree[to];
	}

}

namespace Renderer {
	RenderGraph::RenderGraph(nvrhi::DeviceHandle device)
		: m_Device(device), m_Pool(device) {
	}

	void RenderGraph::Reset() {
		m_Registry.Clear();
		m_Nodes.clear();
		m_Blackboard.Clear();
		m_LastError.clear();
		m_Compiled = false;
	}

	bool RenderGraph::AddPass(std::unique_ptr<IRenderPass> pass) {
		if (!pass) {
			m_LastError = "Cannot add a null render pass";
			return false;
		}

		RGPassDependencies deps;
		RGBuilder builder(m_Registry, deps);
		pass->Setup(builder, m_Blackboard);
		if (!deps.valid) {
			m_LastError = pass->GetName() + ": " + deps.error;
			return false;
		}

		RGPassNode node;
		node.pass = std::move(pass);
		node.deps = std::move(deps);
		m_Nodes.push_back(std::move(node));
		m_Compiled = false;
		return true;
	}

	bool RenderGraph::Compile() {
		m_LastError.clear();
		m_Compiled = false;

		const size_t passCount = m_Nodes.size();
		if (passCount == 0) {
			m_Compiled = true;
			return true;
		}

		std::vector<bool> active(passCount, false);
		std::vector<bool> requiredResource(m_Registry.GetAllResources().size(), false);

		for (size_t passIndex = 0; passIndex < passCount; ++passIndex) {
			const auto& node = m_Nodes[passIndex];
			if (node.pass->HasSideEffect()) active[passIndex] = true;
			const RGResourceID output = node.pass->GetOutputID();
			if (output.isValid()) {
				if (!m_Registry.IsValid(output)) {
					m_LastError = node.pass->GetName() + ": GetOutputID returned an invalid resource";
					return false;
				}
				requiredResource[output.value] = true;
			}
		}

		bool changed = true;
		while (changed) {
			changed = false;
			for (size_t passIndex = 0; passIndex < passCount; ++passIndex) {
				const auto& node = m_Nodes[passIndex];
				const bool producesRequiredResource = std::any_of(node.deps.outputs.begin(), node.deps.outputs.end(),
					[&requiredResource](RGResourceID id) { return id.isValid() && requiredResource[id.value]; });
				if (!active[passIndex] && producesRequiredResource) {
					active[passIndex] = true;
					changed = true;
				}
				if (active[passIndex]) {
					for (const RGResourceID input : node.deps.inputs) {
						if (input.isValid() && !requiredResource[input.value]) {
							requiredResource[input.value] = true;
							changed = true;
						}
					}
				}
			}
		}

	std::vector<std::vector<size_t>> adjacency(passCount);
		std::vector<int> inDegree(passCount, 0);
		std::vector<int> lastWriter(m_Registry.GetAllResources().size(), -1);
		int lastSideEffectPass = -1;

		for (size_t passIndex = 0; passIndex < passCount; ++passIndex) {
			if (!active[passIndex]) continue;
			const auto& node = m_Nodes[passIndex];
			if (node.pass->HasSideEffect()) {
				if (lastSideEffectPass >= 0) {
					AddEdge(adjacency, inDegree, static_cast<size_t>(lastSideEffectPass), passIndex);
				}
				lastSideEffectPass = static_cast<int>(passIndex);
			}

			for (const RGResourceID id : node.deps.inputs) {
				if (!m_Registry.IsValid(id)) {
					m_LastError = node.pass->GetName() + ": input references an invalid resource ID";
					return false;
				}
				if (lastWriter[id.value] >= 0) {
					AddEdge(adjacency, inDegree, static_cast<size_t>(lastWriter[id.value]), passIndex);
				}
				else if (!m_Registry.GetResource(id).isImported) {
					m_LastError = node.pass->GetName() + ": reads a transient resource with no producer";
					return false;
				}
			}

			for (const RGResourceID id : node.deps.outputs) {
				if (!m_Registry.IsValid(id)) {
					m_LastError = node.pass->GetName() + ": output references an invalid resource ID";
					return false;
				}
				if (lastWriter[id.value] >= 0) {
					std::cerr << "WARN: Render graph resource " << id.value << " is written by multiple passes; preserving declaration order." << std::endl;
					AddEdge(adjacency, inDegree, static_cast<size_t>(lastWriter[id.value]), passIndex);
				}
				lastWriter[id.value] = static_cast<int>(passIndex);
			}
		}

		std::queue<size_t> ready;
		for (size_t passIndex = 0; passIndex < passCount; ++passIndex) {
			if (active[passIndex] && inDegree[passIndex] == 0) ready.push(passIndex);
		}

		std::vector<size_t> sortedIndices;
		sortedIndices.reserve(passCount);
		while (!ready.empty()) {
			const size_t passIndex = ready.front();
			ready.pop();
			sortedIndices.push_back(passIndex);
			for (const size_t dependent : adjacency[passIndex]) {
				if (--inDegree[dependent] == 0) ready.push(dependent);
			}
		}

		const size_t activeCount = static_cast<size_t>(std::count(active.begin(), active.end(), true));
		if (sortedIndices.size() != activeCount) {
			std::ostringstream message;
			message << "Render graph cycle detected in passes: ";
			bool first = true;
			for (size_t passIndex = 0; passIndex < passCount; ++passIndex) {
				if (active[passIndex] && inDegree[passIndex] > 0) {
					if (!first) message << ", ";
					message << m_Nodes[passIndex].pass->GetName();
					first = false;
				}
			}
			m_LastError = message.str();
			return false;
		}

		std::vector<RGPassNode> sortedNodes;
		sortedNodes.reserve(sortedIndices.size());
		for (const size_t passIndex : sortedIndices) {
			sortedNodes.push_back(std::move(m_Nodes[passIndex]));
		}
		m_Nodes = std::move(sortedNodes);

		for (RGResource& resource : m_Registry.GetAllResources()) {
			resource.firstPassUse = UINT32_MAX;
			resource.lastPassUse = 0;
		}
		for (uint32_t passIndex = 0; passIndex < m_Nodes.size(); ++passIndex) {
			const auto markUse = [this, passIndex](RGResourceID id) {
				if (!m_Registry.IsValid(id)) return;
				RGResource& resource = m_Registry.GetResource(id);
				resource.firstPassUse = (std::min)(resource.firstPassUse, passIndex);
				resource.lastPassUse = (std::max)(resource.lastPassUse, passIndex);
			};
			for (const RGResourceID id : m_Nodes[passIndex].deps.inputs) markUse(id);
			for (const RGResourceID id : m_Nodes[passIndex].deps.outputs) markUse(id);
		}

		m_Compiled = true;
		return true;
	}

	bool RenderGraph::Execute(nvrhi::ICommandList* cmd) {
		if (!cmd || !m_Compiled) {
			m_LastError = "Cannot execute a null command list or an uncompiled graph";
			return false;
		}
		m_LastError.clear();
		const float deltaTime = m_Blackboard.Get<float>("DeltaTime", 0.016f);
		RenderContext ctx(m_Device.Get(), &m_Registry, &m_Blackboard, deltaTime);

		for (uint32_t passIndex = 0; passIndex < m_Nodes.size(); ++passIndex) {
			for (RGResource& resource : m_Registry.GetAllResources()) {
				if (resource.isImported || resource.firstPassUse != passIndex) continue;
				resource.physicalHandle = m_Pool.Acquire(resource.desc);
				if (!resource.physicalHandle) {
					for (RGResource& allocated : m_Registry.GetAllResources()) {
						if (!allocated.isImported && allocated.physicalHandle) {
							m_Pool.Release(allocated.desc, allocated.physicalHandle);
							allocated.physicalHandle = nullptr;
						}
					}
					m_LastError = "Failed to allocate a transient render graph texture";
					return false;
				}
			}
			for (RGResource& resource : m_Registry.GetAllResources()) {
				if (resource.isImported && resource.firstPassUse == passIndex && resource.physicalHandle) {
					cmd->beginTrackingTextureState(resource.physicalHandle, nvrhi::AllSubresources, resource.initialState);
				}
			}
			for (const RGTextureAccess& access : m_Nodes[passIndex].deps.accesses) {
				RGResource& resource = m_Registry.GetResource(access.resource);
				if (!resource.physicalHandle) continue;
				cmd->setTextureState(resource.physicalHandle, access.subresources, access.state);
			}
			cmd->commitBarriers();

			m_Nodes[passIndex].pass->Execute(cmd, ctx);

			for (RGResource& resource : m_Registry.GetAllResources()) {
				if (resource.lastPassUse == passIndex && resource.physicalHandle) {
					if (resource.isImported) {
						cmd->setTextureState(resource.physicalHandle, nvrhi::AllSubresources, resource.finalState);
						cmd->commitBarriers();
					} else {
						m_Pool.Release(resource.desc, resource.physicalHandle);
						resource.physicalHandle = nullptr;
					}
				}
			}
		}
		return true;
	}
}
