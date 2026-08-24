#include "renderer/render_graph/RenderView.h"
#include "renderer/Camera.h"
#include "renderer/Viewport.h"

namespace Renderer {

	void RenderView::Initialize(const Renderer::Camera* camera, const Engine::Viewport* viewport) {
		if (!camera || !viewport) return;

		ViewportWidth = viewport->GetWidth();
		ViewportHeight = viewport->GetHeight();

		TargetFramebuffer = viewport->GetFramebuffer();

		float aspect = 1.0f;
		if (ViewportHeight > 0) {
			aspect = static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight);
		}

		ViewMatrix = camera->GetView();
		ProjectionMatrix = camera->GetProjection(aspect);

		ViewProjection = ViewMatrix * ProjectionMatrix;
	}

}