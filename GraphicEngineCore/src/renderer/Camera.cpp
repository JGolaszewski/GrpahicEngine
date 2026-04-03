#include "renderer/camera.h"

namespace Renderer {
	Camera::Camera(float fov, float aspect, float znear, float zfar) {
		m_Projection = DirectX::XMMatrixPerspectiveFovLH(fov, aspect, znear, zfar);
	}

	void Camera::SetPosition(Components::Transform transform) {
		m_Transform = transform;
	}
}
