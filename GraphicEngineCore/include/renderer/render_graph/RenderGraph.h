#pragma once

#include <nvrhi/nvrhi.h>

#include "CoreExport.h"
#include "RGBuilder.h
#include "RGResourcePool.h"
#include "RGResources.h"
#include "RenderPass.h"

namespace Renderer {

	class ENGINE_API RenderGraph {
	private:
		RGResourceRegistry m_Registry;
		RGResourcePool m_Pool;
		nvrhi::DeviceHandle m_Device;
		std::vector<std::unique_ptr<IRenderPass>> m_Passes;

	public:
		RenderGraph(nvrhi::DeviceHandle device);

		void Reset();
		void AddPass(std::unique_ptr<IRenderPass> pass);
		void Compile();
		void Execute(nvrhi::ICommandList* cmd);
	};

}
