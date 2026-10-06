#pragma once

#include <nvrhi/nvrhi.h>
#include <DirectXMath.h>

#include "PipelineCache.h"

namespace Renderer {

    enum class RenderQueueType {
        Opaque,
        Masked,
        Transparent,
        ShadowCaster,
        Skybox,
        UI
    };

    struct RenderPacket {
        nvrhi::GraphicsPipelineHandle Pipeline;
        nvrhi::BindingSetHandle MaterialBindings; 
        nvrhi::BindingSetHandle PerObjectBindings;
        nvrhi::BufferHandle VertexBuffer;
        nvrhi::BufferHandle IndexBuffer;
        uint32_t IndexCount;
        uint32_t VertexCount;
        uint32_t BaseVertexOffset;
        uint32_t StartIndexLocation;
        RenderQueueType Queue;
        float CameraDepth = 0.0f;
    };
}
