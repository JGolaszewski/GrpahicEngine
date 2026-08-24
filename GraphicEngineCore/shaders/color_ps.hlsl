#ifndef ENGINE_MAX_LIGHTS
#define ENGINE_MAX_LIGHTS 8
#endif

cbuffer PerObjectCB : register(b0)
{
    float4x4 g_MVP;
    float4x4 g_World;
    float4   g_ObjectParams;
};

// Material buffer
cbuffer MaterialCB : register(b1)
{
    float4 g_BaseColor;
    float4 g_EmissiveMetallic;
    float4 g_SurfaceParams;
};

struct LightData
{
    float4   DirectionIntensity;
    float4   ColorUsePosition;
    float4   PositionRange;
    float4   ConeParams;
    float4x4 LightViewProj;
    float4   ShadowParams;
};

cbuffer LightsCB : register(b2)
{
    LightData g_Lights[ENGINE_MAX_LIGHTS];
    uint      g_LightCount;
    float3    g_LightPadding;
};

Texture2DArray<float>  g_ShadowMaps    : register(t3);
SamplerComparisonState g_ShadowSampler : register(s3);

// Input structure
struct VS_OUTPUT
{
    float4 pos      : SV_POSITION;
    float3 normalWS : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

float SampleShadowPCF(uint lightIndex, float3 worldPos, float3 normalWS, float3 lightVec)
{
    if (g_ObjectParams.x < 0.5f)
    {
        return 1.0f;
    }

    const LightData light           = g_Lights[lightIndex];
    const float     shadowEnabled   = light.ShadowParams.w;
    const float     shadowMapIndex  = light.ShadowParams.x;
    if (shadowEnabled < 0.5f || shadowMapIndex < 0.0f)
    {
        return 1.0f;
    }

    float4 lightClip = mul(float4(worldPos, 1.0f), light.LightViewProj);
    if (abs(lightClip.w) < 1e-5f)
    {
        return 1.0f;
    }

    float3 lightNdc = lightClip.xyz / lightClip.w;
    if (lightNdc.x < -1.0f || lightNdc.x > 1.0f ||
        lightNdc.y < -1.0f || lightNdc.y > 1.0f ||
        lightNdc.z <  0.0f || lightNdc.z > 1.0f)
    {
        return 1.0f;
    }

    uint shadowWidth;
    uint shadowHeight;
    uint shadowElements;
    uint shadowLevels;
    g_ShadowMaps.GetDimensions(0, shadowWidth, shadowHeight, shadowElements, shadowLevels);

    float2 uv        = lightNdc.xy * float2(0.5f, -0.5f) + 0.5f;
    float2 texelSize = 1.0f / float2(max(shadowWidth, 1), max(shadowHeight, 1));

    const float NdotL        = saturate(dot(normalWS, lightVec));
    const float bias         = light.ShadowParams.y + (1.0f - NdotL) * light.ShadowParams.z;
    const float compareDepth = saturate(lightNdc.z - bias);

    const float slice      = shadowMapIndex;
    float       visibility = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 sampleUv = uv + float2((float)x, (float)y) * texelSize;
            visibility += g_ShadowMaps.SampleCmpLevelZero(g_ShadowSampler, float3(sampleUv, slice), compareDepth);
        }
    }

    return visibility / 9.0f;
}

// MAIN PIXEL SHADER FUNCTION
float4 main_ps(VS_OUTPUT input) : SV_TARGET
{
    if (g_ObjectParams.y > 0.5f)
    {
        return float4(g_BaseColor.rgb + g_EmissiveMetallic.rgb, g_BaseColor.a);
    }

    float3 N        = normalize(input.normalWS);
    float  metallic = saturate(g_EmissiveMetallic.w);
    float  roughness = saturate(g_SurfaceParams.x);

    float3 diffuseColor  = g_BaseColor.rgb * (1.0f - metallic);
    float3 specularColor = lerp(float3(0.04f, 0.04f, 0.04f), g_BaseColor.rgb, metallic);
    float  gloss         = 1.0f - roughness;
    float  specularPower = lerp(8.0f, 128.0f, gloss);

    float3 diffuseAccum = 0.0f;
    float3 specAccum    = 0.0f;

    [loop]
    for (uint i = 0; i < g_LightCount; ++i)
    {
        float3 lightDir   = normalize(g_Lights[i].DirectionIntensity.xyz);
        float  intensity  = g_Lights[i].DirectionIntensity.w;
        float3 lightColor = g_Lights[i].ColorUsePosition.rgb;
        float  usePosition = g_Lights[i].ColorUsePosition.w;

        float3 L            = normalize(-lightDir);
        float  attenuation  = 1.0f;

        if (usePosition > 0.5f)
        {
            float3 lightPosition = g_Lights[i].PositionRange.xyz;
            float  range         = max(g_Lights[i].PositionRange.w, 0.001f);
            float3 toLight       = lightPosition - input.worldPos;
            float  distance      = length(toLight);

            if (distance > range)
            {
                continue;
            }

            L = toLight / max(distance, 0.0001f);
            float radial = saturate(1.0f - (distance / range));
            attenuation *= radial * radial;

            float3 lightToPixel    = normalize(input.worldPos - lightPosition);
            float  cone            = dot(lightDir, lightToPixel);
            float  innerCos        = g_Lights[i].ConeParams.x;
            float  outerCos        = g_Lights[i].ConeParams.y;
            float  coneAttenuation = smoothstep(outerCos, innerCos, cone);
            attenuation *= coneAttenuation;
        }

        float lambert  = max(dot(N, L), 0.0f);
        float specular = pow(lambert, specularPower);
        float shadow   = SampleShadowPCF(i, input.worldPos, N, L);

        diffuseAccum += diffuseColor  * lightColor * (lambert  * intensity * attenuation * shadow);
        specAccum    += specularColor * lightColor * (specular * intensity * attenuation * shadow);
    }

    float3 ambient  = 0.04f * diffuseColor;
    float3 emissive = g_EmissiveMetallic.rgb;

    return float4(ambient + diffuseAccum + specAccum + emissive, g_BaseColor.a);
}
