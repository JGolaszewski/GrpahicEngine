#pragma once

#include "CoreExport.h"
#include "renderer/render_graph/FrameBlackboard.h"
#include "renderer/render_graph/RGResourcePool.h"
#include "renderer/render_graph/RenderView.h"

#include <deque>
#include <memory>
#include <vector>

namespace Renderer {
	struct FrameDiagnostics {
		uint64_t UploadBytes = 0;
		uint32_t BufferAllocations = 0;
		uint32_t BindingSetAllocations = 0;
		uint32_t DrawCalls = 0;
	};

	class ENGINE_API FrameContext {
	public:
		static constexpr uint64_t FramesInFlight = 2;

		explicit FrameContext(nvrhi::IDevice* device);
		FrameContext(const FrameContext&) = delete;
		FrameContext& operator=(const FrameContext&) = delete;

		void BeginFrame();
		void MarkSubmitted();

		uint64_t GetFrameIndex() const noexcept { return m_FrameIndex; }
		uint64_t GetSubmittedFrames() const noexcept { return m_SubmittedFrames; }
		uint64_t GetCompletedFrames() const noexcept { return m_CompletedFrames; }
		FrameBlackboard& GetBlackboard() noexcept { return m_Blackboard; }
		const FrameBlackboard& GetBlackboard() const noexcept { return m_Blackboard; }
		RGResourcePool& GetTransientPool() noexcept { return m_TransientPool; }
		std::vector<std::unique_ptr<RenderView>>& GetViews() noexcept { return m_Views; }
		const std::vector<std::unique_ptr<RenderView>>& GetViews() const noexcept { return m_Views; }
		FrameDiagnostics& GetDiagnostics() noexcept { return m_Diagnostics; }

	private:
		struct InFlightFrame {
			uint64_t Frame = 0;
			nvrhi::EventQueryHandle CompletionQuery;
		};

		nvrhi::DeviceHandle m_Device;
		FrameBlackboard m_Blackboard;
		RGResourcePool m_TransientPool;
		std::vector<std::unique_ptr<RenderView>> m_Views;
		std::deque<InFlightFrame> m_InFlightFrames;
		std::vector<nvrhi::EventQueryHandle> m_FreeCompletionQueries;
		FrameDiagnostics m_Diagnostics;
		uint64_t m_FrameIndex = 0;
		uint64_t m_SubmittedFrames = 0;
		uint64_t m_CompletedFrames = 0;
	};
}
