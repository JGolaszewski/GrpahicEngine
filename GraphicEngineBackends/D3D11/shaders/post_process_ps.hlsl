Texture2D<float4> g_InputColor : register(t0);

cbuffer CameraPostProcessConstants : register(b0)
{
    uint g_EffectType;
    float g_Exposure;
    float g_Contrast;
    float g_Saturation;
    float g_Threshold;
    float g_Intensity;
    float g_Radius;
    float g_Padding;
};

static const float3 kLuminance = float3(0.2126f, 0.7152f, 0.0722f);

float3 ExtractBright(float3 color)
{
    const float luminance = max(dot(color, kLuminance), 0.0f);
    const float contribution = saturate((luminance - g_Threshold) / max(luminance, 1.0e-5f));
    return max(color, 0.0f) * contribution;
}

float4 main_ps(float4 position : SV_Position) : SV_Target0
{
    const int2 pixel = int2(position.xy);
    const float4 source = g_InputColor.Load(int3(pixel, 0));

    if (g_EffectType == 2)
    {
        return source;
    }
    if (g_EffectType == 0)
    {
        float3 graded = source.rgb * exp2(g_Exposure);
        graded = 0.18f + (graded - 0.18f) * g_Contrast;
        const float luminance = dot(graded, kLuminance);
        graded = lerp(luminance.xxx, graded, g_Saturation);
        return float4(graded, source.a);
    }

    if (g_Radius <= 0.0f || g_Intensity <= 0.0f)
    {
        return source;
    }

    uint width;
    uint height;
    g_InputColor.GetDimensions(width, height);
    const int radius = max(1, (int)round(g_Radius));
    const int2 maximum = int2(width, height) - 1;
    const int2 offsets[9] = {
        int2(-radius, -radius), int2(0, -radius), int2(radius, -radius),
        int2(-radius, 0),       int2(0, 0),       int2(radius, 0),
        int2(-radius, radius),  int2(0, radius),  int2(radius, radius)
    };
    const float weights[9] = {1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f,
                               2.0f / 16.0f, 4.0f / 16.0f, 2.0f / 16.0f,
                               1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f};
    float3 glow = 0.0f;
    [unroll]
    for (int sampleIndex = 0; sampleIndex < 9; ++sampleIndex)
    {
        const int2 samplePixel = clamp(pixel + offsets[sampleIndex], int2(0, 0), maximum);
        glow += ExtractBright(g_InputColor.Load(int3(samplePixel, 0)).rgb) * weights[sampleIndex];
    }
    const float3 bloom = glow * g_Intensity;
    const float coverage = saturate(dot(bloom, kLuminance));
    const float alpha = 1.0f - (1.0f - saturate(source.a)) * (1.0f - coverage);
    return float4(source.rgb + bloom, alpha);
}
