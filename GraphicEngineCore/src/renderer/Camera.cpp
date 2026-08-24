#include "renderer/camera.h"

namespace Renderer {
	Camera::Camera(float fov, float znear, float zfar)
		: m_Fov(fov), m_Near(znear), m_Far(zfar)
	{
	}

	void Camera::SetPosition(Components::Transform transform) {
		m_Transform = transform;
	}

	DirectX::XMMATRIX Camera::GetView() const
	{
		return DirectX::XMMatrixInverse(nullptr, m_Transform.GetWorldMat());
	}

	DirectX::XMMATRIX Camera::GetProjection(float aspect) const
	{
		return DirectX::XMMatrixPerspectiveFovLH(m_Fov, aspect, m_Near, m_Far);
	}

	DirectX::XMMATRIX Camera::GetViewProjection(float aspect) const
	{
		auto view = DirectX::XMMatrixInverse(nullptr, m_Transform.GetWorldMat());
		auto projection = DirectX::XMMatrixPerspectiveFovLH(m_Fov, aspect, m_Near, m_Far);
		return view * projection;
	}

}
