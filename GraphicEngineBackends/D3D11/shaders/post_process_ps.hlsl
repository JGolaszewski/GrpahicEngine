Texture2D<float4> g_InputColor : register(t0);
SamplerState g_InputSampler : register(s0);

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

float Luma(float3 color)
{
    return dot(color, kLuminance);
}

float4 ApplyFxaa(float2 uv, float2 inverseSize, float4 center)
{
    const float3 northWest = g_InputColor.SampleLevel(g_InputSampler, uv + float2(-1.0f, -1.0f) * inverseSize, 0.0f).rgb;
    const float3 northEast = g_InputColor.SampleLevel(g_InputSampler, uv + float2( 1.0f, -1.0f) * inverseSize, 0.0f).rgb;
    const float3 southWest = g_InputColor.SampleLevel(g_InputSampler, uv + float2(-1.0f,  1.0f) * inverseSize, 0.0f).rgb;
    const float3 southEast = g_InputColor.SampleLevel(g_InputSampler, uv + float2( 1.0f,  1.0f) * inverseSize, 0.0f).rgb;
    const float lumaCenter = Luma(center.rgb);
    const float lumaNorthWest = Luma(northWest);
    const float lumaNorthEast = Luma(northEast);
    const float lumaSouthWest = Luma(southWest);
    const float lumaSouthEast = Luma(southEast);
    const float lumaMinimum = min(lumaCenter, min(min(lumaNorthWest, lumaNorthEast), min(lumaSouthWest, lumaSouthEast)));
    const float lumaMaximum = max(lumaCenter, max(max(lumaNorthWest, lumaNorthEast), max(lumaSouthWest, lumaSouthEast)));
    const float contrast = lumaMaximum - lumaMinimum;
    if (contrast < max(1.0f / 32.0f, lumaMaximum * 1.0f / 8.0f))
    {
        return center;
    }

    float2 direction = float2(-((lumaNorthWest + lumaNorthEast) - (lumaSouthWest + lumaSouthEast)),
                               (lumaNorthWest + lumaSouthWest) - (lumaNorthEast + lumaSouthEast));
    const float directionReduce = max((lumaNorthWest + lumaNorthEast + lumaSouthWest + lumaSouthEast) *
                                      (0.25f / 8.0f), 1.0f / 128.0f);
    const float inverseMinimum = 1.0f / (min(abs(direction.x), abs(direction.y)) + directionReduce);
    direction = clamp(direction * inverseMinimum, -8.0f, 8.0f) * inverseSize;

    const float3 filteredA = 0.5f * (
        g_InputColor.SampleLevel(g_InputSampler, uv + direction * (1.0f / 3.0f - 0.5f), 0.0f).rgb +
        g_InputColor.SampleLevel(g_InputSampler, uv + direction * (2.0f / 3.0f - 0.5f), 0.0f).rgb);
    const float3 filteredB = filteredA * 0.5f + 0.25f * (
        g_InputColor.SampleLevel(g_InputSampler, uv + direction * -0.5f, 0.0f).rgb +
        g_InputColor.SampleLevel(g_InputSampler, uv + direction * 0.5f, 0.0f).rgb);
    const float lumaFilteredB = Luma(filteredB);
    const float3 filtered = lumaFilteredB < lumaMinimum || lumaFilteredB > lumaMaximum ? filteredA : filteredB;
    return float4(filtered, center.a);
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
    if (g_EffectType == 3)
    {
        uint width;
        uint height;
        g_InputColor.GetDimensions(width, height);
        return ApplyFxaa(position.xy / float2(width, height), 1.0f / float2(width, height), source);
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
