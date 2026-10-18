#include "display_transform.hlsli"

Texture2D<float4> g_LinearColor : register(t0);

float4 main_ps(float4 position : SV_Position) : SV_Target0
{
    const float4 linearColor = g_LinearColor.Load(int3(int2(position.xy), 0));
    return float4(ApplyDisplayTransform(linearColor.rgb), linearColor.a);
}
