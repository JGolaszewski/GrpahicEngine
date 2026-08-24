#pragma once

#include <nvrhi/nvrhi.h>
#include <vector>
#include <memory>

#include "CoreExport.h"
#include "RGBuilder.h"
#include "RGResourcePool.h"
#include "RGResources.h"
#include "RenderPass.h"
#include "FrameBlackboard.h"
#include "RenderContext.h"

namespace Renderer {

	struct RGPassNode {
		std::unique_ptr<IRenderPass> pass;
		RGPassDependencies deps;

		RGPassNode() = default;

		// Block copying
		RGPassNode(const RGPassNode&) = delete;
		RGPassNode& operator=(const RGPassNode&) = delete;

		// Move allow
		RGPassNode(RGPassNode&&) noexcept = default;
		RGPassNode& operator=(RGPassNode&&) noexcept = default;
	};

	class ENGINE_API RenderGraph {
	private:
		RGResourceRegistry m_Registry;
		RGResourcePool m_Pool;
		FrameBlackboard m_Blackboard;
		nvrhi::DeviceHandle m_Device;
		std::vector<RGPassNode> m_Nodes;

	public:
		RenderGraph(nvrhi::DeviceHandle device);

		FrameBlackboard& GetBlackboard() { return m_Blackboard; }

		void Reset();
		void AddPass(std::unique_ptr<IRenderPass> pass);
		void Compile();
		void Execute(nvrhi::ICommandList* cmd);

		RenderGraph(const RenderGraph&) = delete;
		RenderGraph& operator=(const RenderGraph&) = delete;

		RenderGraph(RenderGraph&&) noexcept = default;
		RenderGraph& operator=(RenderGraph&&) noexcept = default;
	};

}
