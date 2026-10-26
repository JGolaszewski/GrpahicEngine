Texture2D<float4> g_InputColor : register(t0);
Texture2D<float4> g_BloomColor : register(t1);
SamplerState g_InputSampler : register(s0);

cbuffer CameraPostProcessConstants : register(b0)
{
    uint g_EffectType;
    uint g_FxaaQuality;
    uint g_BloomExtractBright;
    uint g_SmoothBloom;
    float g_Exposure;
    float g_Contrast;
    float g_Saturation;
    float g_Threshold;
    float g_Intensity;
    float g_Radius;
    float g_EdgeThreshold;
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
    if (contrast < max(1.0f / 32.0f, lumaMaximum * g_EdgeThreshold))
    {
        return center;
    }

    float2 direction = float2(-((lumaNorthWest + lumaNorthEast) - (lumaSouthWest + lumaSouthEast)),
                               (lumaNorthWest + lumaSouthWest) - (lumaNorthEast + lumaSouthEast));
    const float directionReduce = max((lumaNorthWest + lumaNorthEast + lumaSouthWest + lumaSouthEast) *
                                      (0.25f / 8.0f), 1.0f / 128.0f);
    const float inverseMinimum = 1.0f / (min(abs(direction.x), abs(direction.y)) + directionReduce);
    const float maxSpan = g_FxaaQuality == 0u ? 4.0f : (g_FxaaQuality == 2u ? 12.0f : 8.0f);
    direction = clamp(direction * inverseMinimum, -maxSpan, maxSpan) * inverseSize;

    const float3 filteredA = 0.5f * (
        g_InputColor.SampleLevel(g_InputSampler, uv + direction * (1.0f / 3.0f - 0.5f), 0.0f).rgb +
        g_InputColor.SampleLevel(g_InputSampler, uv + direction * (2.0f / 3.0f - 0.5f), 0.0f).rgb);
    if (g_FxaaQuality == 0u)
    {
        return float4(filteredA, center.a);
    }
    const float3 filteredB = filteredA * 0.5f + 0.25f * (
        g_InputColor.SampleLevel(g_InputSampler, uv + direction * -0.5f, 0.0f).rgb +
        g_InputColor.SampleLevel(g_InputSampler, uv + direction * 0.5f, 0.0f).rgb);
    const float lumaFilteredB = Luma(filteredB);
    const float3 filtered = lumaFilteredB < lumaMinimum || lumaFilteredB > lumaMaximum ? filteredA : filteredB;
    return float4(filtered, center.a);
}

float3 Prefilter(float3 color)
{
    return g_BloomExtractBright != 0u ? ExtractBright(color) : color;
}

float4 main_ps(float4 position : SV_Position) : SV_Target0
{
    const int2 pixel = int2(position.xy);
    const float4 source = g_InputColor.Load(int3(pixel, 0));

    if (g_EffectType == 2u)
    {
        return source;
    }
    if (g_EffectType == 0u)
    {
        float3 graded = source.rgb * exp2(g_Exposure);
        graded = 0.18f + (graded - 0.18f) * g_Contrast;
        const float luminance = dot(graded, kLuminance);
        graded = lerp(luminance.xxx, graded, g_Saturation);
        return float4(graded, source.a);
    }
    if (g_EffectType == 3u)
    {
        uint width;
        uint height;
        g_InputColor.GetDimensions(width, height);
        return ApplyFxaa(position.xy / float2(width, height), 1.0f / float2(width, height), source);
    }
    if (g_EffectType == 4u)
    {
        uint width;
        uint height;
        g_InputColor.GetDimensions(width, height);
        const float2 inverseSize = 1.0f / float2(width, height);
        const float2 uv = position.xy / float2(max(width / 2u, 1u), max(height / 2u, 1u));
        if (g_SmoothBloom != 0u)
        {
            float3 sum = 0.0f;
            [unroll]
            for (int y = -1; y <= 1; ++y)
            {
                [unroll]
                for (int x = -1; x <= 1; ++x)
                {
                    const float weight = (x == 0 ? 2.0f : 1.0f) * (y == 0 ? 2.0f : 1.0f);
                    sum += Prefilter(g_InputColor.SampleLevel(g_InputSampler, uv + float2(x, y) * inverseSize, 0.0f).rgb) * weight;
                }
            }
            return float4(sum / 16.0f, 0.0f);
        }
        const float3 a = Prefilter(g_InputColor.SampleLevel(g_InputSampler, uv + float2(-0.5f, -0.5f) * inverseSize, 0.0f).rgb);
        const float3 b = Prefilter(g_InputColor.SampleLevel(g_InputSampler, uv + float2( 0.5f, -0.5f) * inverseSize, 0.0f).rgb);
        const float3 c = Prefilter(g_InputColor.SampleLevel(g_InputSampler, uv + float2(-0.5f,  0.5f) * inverseSize, 0.0f).rgb);
        const float3 d = Prefilter(g_InputColor.SampleLevel(g_InputSampler, uv + float2( 0.5f,  0.5f) * inverseSize, 0.0f).rgb);
        return float4((a + b + c + d) * 0.25f, 0.0f);
    }
    if (g_EffectType == 5u || g_EffectType == 6u)
    {
        if (g_Intensity <= 0.0f || g_Radius <= 0.0f)
        {
            return source;
        }
        uint bloomWidth;
        uint bloomHeight;
        g_BloomColor.GetDimensions(bloomWidth, bloomHeight);
        uint inputWidth;
        uint inputHeight;
        g_InputColor.GetDimensions(inputWidth, inputHeight);
        const float2 uv = position.xy / float2(inputWidth, inputHeight);
        float3 glow;
        if (g_SmoothBloom != 0u)
        {
            const float2 inverseBloomSize = 1.0f / float2(bloomWidth, bloomHeight);
            const float3 northWest = g_BloomColor.SampleLevel(g_InputSampler, uv + float2(-1.0f, -1.0f) * inverseBloomSize, 0.0f).rgb;
            const float3 north = g_BloomColor.SampleLevel(g_InputSampler, uv + float2(0.0f, -1.0f) * inverseBloomSize, 0.0f).rgb;
            const float3 northEast = g_BloomColor.SampleLevel(g_InputSampler, uv + float2(1.0f, -1.0f) * inverseBloomSize, 0.0f).rgb;
            const float3 west = g_BloomColor.SampleLevel(g_InputSampler, uv + float2(-1.0f, 0.0f) * inverseBloomSize, 0.0f).rgb;
            const float3 center = g_BloomColor.SampleLevel(g_InputSampler, uv, 0.0f).rgb;
            const float3 east = g_BloomColor.SampleLevel(g_InputSampler, uv + float2(1.0f, 0.0f) * inverseBloomSize, 0.0f).rgb;
            const float3 southWest = g_BloomColor.SampleLevel(g_InputSampler, uv + float2(-1.0f, 1.0f) * inverseBloomSize, 0.0f).rgb;
            const float3 south = g_BloomColor.SampleLevel(g_InputSampler, uv + float2(0.0f, 1.0f) * inverseBloomSize, 0.0f).rgb;
            const float3 southEast = g_BloomColor.SampleLevel(g_InputSampler, uv + float2(1.0f, 1.0f) * inverseBloomSize, 0.0f).rgb;
            glow = (northWest + northEast + southWest + southEast + 2.0f * (north + west + east + south) + 4.0f * center) / 16.0f;
        }
        else
        {
            glow = g_BloomColor.SampleLevel(g_InputSampler, uv, 0.0f).rgb;
        }
        if (g_EffectType == 5u)
        {
            return float4(source.rgb + glow, source.a);
        }
        const float3 bloom = glow * g_Intensity;
        const float coverage = saturate(dot(bloom, kLuminance));
        const float alpha = 1.0f - (1.0f - saturate(source.a)) * (1.0f - coverage);
        return float4(source.rgb + bloom, alpha);
    }
    return source;
}
