#pragma once

#include <nvrhi/nvrhi.h>
#include <vector>
#include <memory>
#include <string>

#include "CoreExport.h"
#include "RGBuilder.h"
#include "RGResourcePool.h"
#include "RGResources.h"
#include "RenderPass.h"
#include "FrameBlackboard.h"
#include "RenderContext.h"
#include "FrameContext.h"

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
		FrameContext& m_FrameContext;
		nvrhi::DeviceHandle m_Device;
		std::vector<RGPassNode> m_Nodes;
		std::string m_LastError;
		bool m_Compiled = false;

	public:
		RenderGraph(nvrhi::DeviceHandle device, FrameContext& frameContext);

		FrameBlackboard& GetBlackboard() { return m_FrameContext.GetBlackboard(); }

		void Reset();
		bool AddPass(std::unique_ptr<IRenderPass> pass);
		bool Compile();
		bool Execute(nvrhi::ICommandList* cmd);
		const std::string& GetLastError() const noexcept { return m_LastError; }
		std::string Dump() const;

		RenderGraph(const RenderGraph&) = delete;
		RenderGraph& operator=(const RenderGraph&) = delete;

		RenderGraph(RenderGraph&&) noexcept = default;
		RenderGraph& operator=(RenderGraph&&) noexcept = default;
	};

}
