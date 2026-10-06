#ifndef ENGINE_MAX_LIGHTS
#define ENGINE_MAX_LIGHTS 8
#endif

#include "brdf_common.hlsli"

cbuffer PerObjectCB : register(b0)
{
    float4x4 g_MVP;
    float4x4 g_World;
    float4x4 g_NormalMatrix;
    float4   g_ObjectParams;
};

// Material buffer
cbuffer MaterialCB : register(b1)
{
    float4 g_BaseColor;
    float4 g_EmissiveMetallic;
    float4 g_SurfaceParams;
    float4 g_SpecularParams;
    uint4  g_ModelFlags;
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

cbuffer CameraCB : register(b4)
{
    float4 g_CameraPosition;
};

Texture2DArray<float>  g_ShadowMaps    : register(t3);
SamplerComparisonState g_ShadowSampler : register(s3);

Texture2D<float4> g_BaseColorMap : register(t0);
SamplerState g_BaseColorSampler : register(s0);
Texture2D<float4> g_NormalMap : register(t1);
SamplerState g_NormalSampler : register(s1);

// Input structure
struct VS_OUTPUT
{
    float4 pos      : SV_POSITION;
    float3 normalWS : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 tangentWS : TEXCOORD3;
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
    const float4 baseColorSample = g_BaseColorMap.Sample(g_BaseColorSampler, input.uv);
    const float3 baseColor = g_BaseColor.rgb * baseColorSample.rgb;
    if (g_ObjectParams.y > 0.5f)
    {
        return float4(baseColor + g_EmissiveMetallic.rgb, g_BaseColor.a * baseColorSample.a);
    }

    const float3 geometricNormal = BrdfNormalizeOr(input.normalWS, float3(0.0f, 0.0f, 1.0f));
    const float3 tangent = BrdfNormalizeOr(input.tangentWS.xyz - geometricNormal * dot(geometricNormal, input.tangentWS.xyz),
        abs(geometricNormal.y) < 0.9f ? normalize(cross(float3(0.0f, 1.0f, 0.0f), geometricNormal)) : float3(1.0f, 0.0f, 0.0f));
    const float3 bitangent = cross(geometricNormal, tangent) * (input.tangentWS.w < 0.0f ? -1.0f : 1.0f);
    const float3 tangentNormal = g_NormalMap.Sample(g_NormalSampler, input.uv).xyz * 2.0f - 1.0f;
    float3 N = BrdfNormalizeOr(tangent * tangentNormal.x + bitangent * tangentNormal.y + geometricNormal * tangentNormal.z,
        geometricNormal);
    float  metallic = saturate(g_EmissiveMetallic.w);
    const bool isLegacy = g_ModelFlags.y == 0u;
    const float alpha = max(g_SurfaceParams.y, BRDF_MIN_ALPHA);
    const float ior = max(g_SurfaceParams.w, 1.0f);
    const float iorRatio = (ior - 1.0f) / (ior + 1.0f);
    const float3 dielectricF0 = g_ModelFlags.z == 1u
        ? saturate(g_SpecularParams.rgb)
        : float3(iorRatio * iorRatio, iorRatio * iorRatio, iorRatio * iorRatio);
    const float3 legacyDiffuseColor = baseColor * (1.0f - metallic);
    const float3 legacySpecularColor = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
    const float legacySpecularPower = lerp(8.0f, 128.0f, 1.0f - saturate(g_SurfaceParams.x));
    const float3 V = g_CameraPosition.xyz - input.worldPos;

    float3 diffuseAccum = 0.0f;
    float3 specAccum    = 0.0f;
    float3 brdfAccum    = 0.0f;

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

        float shadow   = SampleShadowPCF(i, input.worldPos, N, L);

        if (isLegacy)
        {
            const float lambert = max(dot(N, L), 0.0f);
            const float specular = pow(lambert, legacySpecularPower);
            diffuseAccum += legacyDiffuseColor * lightColor * (lambert * intensity * attenuation * shadow);
            specAccum += legacySpecularColor * lightColor * (specular * intensity * attenuation * shadow);
        }
        else
        {
            const BrdfQuery query = BuildBrdfQuery(N, V, L);
            const float3 brdf = g_ModelFlags.y == 2u
                ? EvaluateGtrBrdf(query, baseColor, metallic, dielectricF0, alpha, g_SurfaceParams.z)
                : EvaluateGgxBrdf(query, baseColor, metallic, dielectricF0, alpha);
            brdfAccum += brdf * lightColor * (query.NoL * intensity * attenuation * shadow);
        }
    }

    const float3 ambient = 0.04f * legacyDiffuseColor;
    float3 emissive = g_EmissiveMetallic.rgb;

    const float3 directLighting = isLegacy ? diffuseAccum + specAccum : brdfAccum;
    return float4(ambient + directLighting + emissive, g_BaseColor.a * baseColorSample.a);
}
