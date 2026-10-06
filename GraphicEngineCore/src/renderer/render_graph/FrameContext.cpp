#include "renderer/render_graph/FrameContext.h"

#include <algorithm>
#include <iostream>

namespace Renderer {
	FrameContext::FrameContext(nvrhi::IDevice* device)
		: m_Device(device), m_TransientPool(device) {
	}

	void FrameContext::BeginFrame() {
		m_FrameIndex = m_SubmittedFrames + 1;
		m_Blackboard.Clear();
		m_Views.clear();
		m_Diagnostics = {};
	}

	void FrameContext::MarkSubmitted() {
		++m_SubmittedFrames;
		if (!m_Device) return;

		nvrhi::EventQueryHandle query;
		if (!m_FreeCompletionQueries.empty()) {
			query = std::move(m_FreeCompletionQueries.back());
			m_FreeCompletionQueries.pop_back();
		} else {
			query = m_Device->createEventQuery();
		}
		if (!query) {
			std::cerr << "ERROR: Could not create a GPU completion query; waiting for idle to retire resources safely." << std::endl;
			m_Device->waitForIdle();
			m_CompletedFrames = (std::max)(m_CompletedFrames, m_SubmittedFrames + FramesInFlight);
			while (!m_InFlightFrames.empty()) {
				auto& inFlight = m_InFlightFrames.front();
				m_Device->resetEventQuery(inFlight.CompletionQuery);
				m_FreeCompletionQueries.push_back(std::move(inFlight.CompletionQuery));
				m_InFlightFrames.pop_front();
			}
			return;
		}

		m_Device->setEventQuery(query, nvrhi::CommandQueue::Graphics);
		m_InFlightFrames.push_back({ m_SubmittedFrames, std::move(query) });
		while (!m_InFlightFrames.empty() && m_Device->pollEventQuery(m_InFlightFrames.front().CompletionQuery)) {
			m_CompletedFrames = (std::max)(m_CompletedFrames, m_InFlightFrames.front().Frame);
			m_Device->resetEventQuery(m_InFlightFrames.front().CompletionQuery);
			m_FreeCompletionQueries.push_back(std::move(m_InFlightFrames.front().CompletionQuery));
			m_InFlightFrames.pop_front();
		}
	}
}
