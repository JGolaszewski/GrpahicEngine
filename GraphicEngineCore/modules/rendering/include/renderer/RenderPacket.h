#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <nvrhi/nvrhi.h>

#include "rendering/include/PipelineCache.h"

namespace Engine::Renderer
{

struct PerObjectGpuData
{
    DirectX::XMFLOAT4X4 MVP;
    DirectX::XMFLOAT4X4 World;
    DirectX::XMFLOAT4X4 Normal;
    DirectX::XMFLOAT4 ObjectParams;
    std::array<uint8_t, 48> Padding{};
};
static_assert(sizeof(PerObjectGpuData) == 256);

enum class RenderQueueType
{
    Opaque,
    Masked,
    Transparent,
    ShadowCaster,
    Skybox,
    UI
};

struct RenderPacket
{
    nvrhi::GraphicsPipelineHandle Pipeline;
    nvrhi::BindingSetHandle MaterialBindings;
    PerObjectGpuData PerObjectData{};
    nvrhi::BufferHandle VertexBuffer;
    nvrhi::BufferHandle IndexBuffer;
    uint32_t IndexCount;
    uint32_t VertexCount;
    uint32_t BaseVertexOffset;
    uint32_t StartIndexLocation;
    RenderQueueType Queue;
    float CameraDepth = 0.0f;
    bool ShadowAlphaTest = false;
};
} // namespace Engine::Renderer
