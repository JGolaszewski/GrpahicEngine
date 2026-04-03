#pragma once

#include "CoreExport.h"
#include <DirectXMath.h>

namespace Components {
	class ENGINE_API Transform {
	public:
		void SetPosition(float x, float y, float z);

		void SetRotation(float pitch, float yaw, float roll);

		void SetScale(float x, float y, float z);

		void Translate(float x, float y, float z);

		void ScaleBy(float x, float y, float z);

		void Rotate(float pitch, float yaw, float roll);

		void LookAt(float targetX, float targetY, float targetZ);

		DirectX::XMMATRIX GetWorldMat() const;
	
		DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT4 Rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
		DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
	};
}