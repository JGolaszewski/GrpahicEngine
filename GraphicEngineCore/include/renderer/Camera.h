#pragma once

#include "CoreExport.h"

#include <DirectXMath.h>

#include <components/Transform.h>

namespace Renderer {
	class ENGINE_API Camera {
	public:
		Components::Transform m_Transform;
		DirectX::XMMATRIX m_Projection;

		Camera(float fov, float aspect, float znear, float zfar);

		void SetPosition(Components::Transform transform);

		DirectX::XMMATRIX GetViewMat() const {
			return DirectX::XMMatrixInverse(nullptr, m_Transform.GetWorldMat());
		}
		DirectX::XMMATRIX GetViewProjection() const { return GetViewMat() * m_Projection; }
	};
}