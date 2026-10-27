cbuffer SkyboxCB : register(b0)
{
    float4 g_CameraForward;
    float4 g_CameraRight;
    float4 g_CameraUp;
    float4 g_Projection; // tan(vertical FOV / 2), aspect ratio, viewport width, viewport height
    float4 g_ColorAndMode; // linear RGB solid color; w is 1 for solid color mode
};

Texture2D<float4> g_EnvironmentPanorama : register(t0);
SamplerState g_EnvironmentSampler : register(s0);

float4 main_ps(float4 position : SV_POSITION) : SV_Target0
{
    if (g_ColorAndMode.w > 0.5f)
    {
        return float4(g_ColorAndMode.rgb, 1.0f);
    }
    const float2 ndc = float2(position.x / g_Projection.z * 2.0f - 1.0f,
                              1.0f - position.y / g_Projection.w * 2.0f);
    const float3 direction = normalize(g_CameraForward.xyz +
                                       g_CameraRight.xyz * (ndc.x * g_Projection.x * g_Projection.y) +
                                       g_CameraUp.xyz * (ndc.y * g_Projection.x));
    const float2 uv = float2(atan2(direction.z, direction.x) / 6.28318530718f + 0.5f,
                             acos(clamp(direction.y, -1.0f, 1.0f)) / 3.14159265359f);
    return float4(g_EnvironmentPanorama.SampleLevel(g_EnvironmentSampler, uv, 0.0f).rgb, 1.0f);
}
