Texture2D<float> g_DepthTexture : register(t0);
Texture2D<float4> g_NormalTexture : register(t1);

cbuffer AmbientOcclusionConstants : register(b0)
{
    float4 g_ClipParams;       // near, far, world radius, intensity
    float4 g_ProjectionParams; // tan(half vertical FOV), aspect, width, height
    float4 g_CameraRight;
    float4 g_CameraUp;
    float4 g_CameraForward;
};

static const float2 kSampleDirections[8] = {
    float2(1.0f, 0.0f), float2(-1.0f, 0.0f),
    float2(0.0f, 1.0f), float2(0.0f, -1.0f),
    float2(0.70710678f, 0.70710678f), float2(-0.70710678f, 0.70710678f),
    float2(0.70710678f, -0.70710678f), float2(-0.70710678f, -0.70710678f)
};

float LinearizeDepth(float depth)
{
    const float nearPlane = g_ClipParams.x;
    const float farPlane = g_ClipParams.y;
    return (nearPlane * farPlane) / max(farPlane - depth * (farPlane - nearPlane), 1.0e-6f);
}

float3 ReconstructViewPosition(float2 pixel, float viewDepth)
{
    const float2 uv = (pixel + 0.5f) / g_ProjectionParams.zw;
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float halfHeight = viewDepth * g_ProjectionParams.x;
    return float3(ndc.x * halfHeight * g_ProjectionParams.y, ndc.y * halfHeight, viewDepth);
}

float4 main_ps(float4 position : SV_Position) : SV_Target0
{
    const int2 pixel = int2(position.xy);
    const int2 maximum = int2(g_ProjectionParams.zw) - 1;
    const float centerDepth = g_DepthTexture.Load(int3(pixel, 0));
    const float4 centerNormalValue = g_NormalTexture.Load(int3(pixel, 0));
    if (centerDepth >= 1.0f || centerNormalValue.w <= 0.0f)
    {
        return 1.0f;
    }

    const float centerViewDepth = LinearizeDepth(centerDepth);
    const float3 centerPosition = ReconstructViewPosition(float2(pixel), centerViewDepth);
    const float3 centerNormal = normalize(centerNormalValue.xyz);
    const float radiusPixels = clamp(g_ClipParams.z / max(centerViewDepth, g_ClipParams.x) *
                                     g_ProjectionParams.w / max(2.0f * g_ProjectionParams.x, 1.0e-5f), 1.0f, 64.0f);
    float occlusion = 0.0f;
    uint sampleCount = 0u;

    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < 8u; ++sampleIndex)
    {
        [unroll]
        for (uint ring = 0u; ring < 2u; ++ring)
        {
            const float ringScale = ring == 0u ? 0.5f : 1.0f;
            const int2 samplePixel = clamp(pixel + int2(round(kSampleDirections[sampleIndex] * radiusPixels * ringScale)),
                                           int2(0, 0), maximum);
            if (all(samplePixel == pixel))
            {
                continue;
            }
            const float sampleDepth = g_DepthTexture.Load(int3(samplePixel, 0));
            const float4 sampleNormalValue = g_NormalTexture.Load(int3(samplePixel, 0));
            if (sampleDepth >= 1.0f || sampleNormalValue.w <= 0.0f)
            {
                continue;
            }

            const float sampleViewDepth = LinearizeDepth(sampleDepth);
            if (sampleViewDepth >= centerViewDepth)
            {
                continue;
            }
            const float3 samplePosition = ReconstructViewPosition(float2(samplePixel), sampleViewDepth);
            const float3 deltaView = samplePosition - centerPosition;
            const float distance = length(deltaView);
            if (distance <= 1.0e-5f || distance >= g_ClipParams.z)
            {
                continue;
            }
            const float3 deltaWorld = g_CameraRight.xyz * deltaView.x + g_CameraUp.xyz * deltaView.y +
                                     g_CameraForward.xyz * deltaView.z;
            const float3 direction = normalize(deltaWorld);
            const float3 sampleNormal = normalize(sampleNormalValue.xyz);
            const float hemisphere = saturate(dot(centerNormal, direction));
            const float normalWeight = saturate(0.5f + 0.5f * dot(centerNormal, sampleNormal));
            const float distanceWeight = 1.0f - smoothstep(0.0f, g_ClipParams.z, distance);
            const float depthWeight = saturate((centerViewDepth - sampleViewDepth) / g_ClipParams.z);
            occlusion += hemisphere * normalWeight * distanceWeight * depthWeight;
            ++sampleCount;
        }
    }

    const float averageOcclusion = sampleCount > 0u ? occlusion / float(sampleCount) : 0.0f;
    return saturate(1.0f - averageOcclusion * g_ClipParams.w * 2.0f);
}
