#pragma once

#include "CoreExport.h"

#include <DirectXMath.h>

#include <components/Transform.h>

namespace Renderer {
	class ENGINE_API Camera {
	public:
		Components::Transform m_Transform;

		Camera(float fov, float znear, float zfar);

		void SetPosition(Components::Transform transform);

		DirectX::XMMATRIX GetView() const;
		DirectX::XMMATRIX GetProjection(float aspect) const;
		DirectX::XMMATRIX GetViewProjection(float aspect) const;

	private:
		float m_Fov = 0.0f;
		float m_Near = 0.0f;
		float m_Far = 0.0f;
	};
}