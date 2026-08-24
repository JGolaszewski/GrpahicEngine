#pragma once

#include "RGResources.h"
#include <nvrhi/nvrhi.h>

namespace Renderer {

    class FrameBlackboard;

    class RenderContext {
    public:
        RenderContext(nvrhi::IDevice* device, RGResourceRegistry* registry, FrameBlackboard* blackboard, float dt);

        nvrhi::TextureHandle GetPhysicalTexture(RGResourceID id) const;
        nvrhi::IDevice* GetDevice() const { return m_Device; }

        inline float GetDeltaTime() const { return m_DeltaTime; }

        FrameBlackboard& GetBlackboard() const { return *m_Blackboard; }

    private:
        nvrhi::IDevice* m_Device;
        RGResourceRegistry* m_Registry;
        FrameBlackboard* m_Blackboard;

        float m_DeltaTime;
    };
}
