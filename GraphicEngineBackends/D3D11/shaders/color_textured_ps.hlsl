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
    float4 g_AlphaParams;
    uint4 g_TextureCoordinates0;
    uint4 g_TextureCoordinates1;
    float4 g_TextureParams;
    float4 g_TextureTransformRows[5];
    float4 g_TextureTransformOffsets[5];
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

Texture2DArray<float>  g_ShadowMaps    : register(t6);
SamplerComparisonState g_ShadowSampler : register(s6);
Texture2D<float4> g_IrradianceMap : register(t7);
SamplerState g_IrradianceSampler : register(s7);
Texture2D<float4> g_PrefilteredEnvironment : register(t8);
SamplerState g_PrefilteredEnvironmentSampler : register(s8);
Texture2D<float2> g_BrdfIntegrationLut : register(t9);
SamplerState g_BrdfIntegrationSampler : register(s9);

Texture2D<float4> g_BaseColorMap : register(t0);
SamplerState g_BaseColorSampler : register(s0);
Texture2D<float4> g_NormalMap : register(t1);
SamplerState g_NormalSampler : register(s1);
Texture2D<float4> g_MetallicRoughnessMap : register(t2);
SamplerState g_MetallicRoughnessSampler : register(s2);
Texture2D<float4> g_OcclusionMap : register(t4);
SamplerState g_OcclusionSampler : register(s4);
Texture2D<float4> g_EmissiveMap : register(t5);
SamplerState g_EmissiveSampler : register(s5);

// Input structure
struct VS_OUTPUT
{
    float4 pos      : SV_POSITION;
    float3 normalWS : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 tangentWS : TEXCOORD3;
    float2 uv1 : TEXCOORD4;
    float4 color : TEXCOORD5;
    float2 uv2 : TEXCOORD6;
    float2 uv3 : TEXCOORD7;
    float2 uv4 : TEXCOORD8;
    float2 uv5 : TEXCOORD9;
    float2 uv6 : TEXCOORD10;
    float2 uv7 : TEXCOORD11;
};

float2 GetTextureCoordinate(uint index, VS_OUTPUT input)
{
    switch (index)
    {
    case 0u: return input.uv;
    case 1u: return input.uv1;
    case 2u: return input.uv2;
    case 3u: return input.uv3;
    case 4u: return input.uv4;
    case 5u: return input.uv5;
    case 6u: return input.uv6;
    default: return input.uv7;
    }
}

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

    uint shadowWidth;
    uint shadowHeight;
    uint shadowElements;
    uint shadowLevels;
    g_ShadowMaps.GetDimensions(0, shadowWidth, shadowHeight, shadowElements, shadowLevels);

    float2 texelSize = 1.0f / float2(max(shadowWidth, 1), max(shadowHeight, 1));

    const float NdotL        = saturate(dot(normalWS, lightVec));
    const float bias         = light.ShadowParams.y + (1.0f - NdotL) * light.ShadowParams.z;
    float2 uv = 0.0f;
    float compareDepth = 1.0f;
    float slice = shadowMapIndex;
    if (light.ConeParams.z > 0.5f)
    {
        const float3 fromLight = worldPos - light.PositionRange.xyz;
        const float3 magnitude = abs(fromLight);
        const float major = max(magnitude.x, max(magnitude.y, magnitude.z));
        if (major <= 1e-5f || major > light.PositionRange.w)
        {
            return 1.0f;
        }
        float2 faceUv;
        if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
        {
            if (fromLight.x >= 0.0f) { slice += 0.0f; faceUv = float2(-fromLight.z, -fromLight.y) / major; }
            else                    { slice += 1.0f; faceUv = float2(fromLight.z, -fromLight.y) / major; }
        }
        else if (magnitude.y >= magnitude.z)
        {
            if (fromLight.y >= 0.0f) { slice += 2.0f; faceUv = float2(fromLight.x, fromLight.z) / major; }
            else                    { slice += 3.0f; faceUv = float2(fromLight.x, -fromLight.z) / major; }
        }
        else
        {
            if (fromLight.z >= 0.0f) { slice += 4.0f; faceUv = float2(fromLight.x, -fromLight.y) / major; }
            else                    { slice += 5.0f; faceUv = float2(-fromLight.x, -fromLight.y) / major; }
        }
        uv = faceUv * 0.5f + 0.5f;
        const float nearPlane = 0.05f;
        const float farPlane = max(light.PositionRange.w, nearPlane + 0.01f);
        const float pointDepth = farPlane / (farPlane - nearPlane) -
                                 (farPlane * nearPlane) / ((farPlane - nearPlane) * major);
        compareDepth = saturate(pointDepth - bias);
    }
    else
    {
        const float4 lightClip = mul(float4(worldPos, 1.0f), light.LightViewProj);
        if (abs(lightClip.w) < 1e-5f) return 1.0f;
        const float3 lightNdc = lightClip.xyz / lightClip.w;
        if (lightNdc.x < -1.0f || lightNdc.x > 1.0f || lightNdc.y < -1.0f || lightNdc.y > 1.0f ||
            lightNdc.z < 0.0f || lightNdc.z > 1.0f) return 1.0f;
        uv = lightNdc.xy * float2(0.5f, -0.5f) + 0.5f;
        compareDepth = saturate(lightNdc.z - bias);
    }
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
struct PS_OUTPUT
{
    float4 FinalColor : SV_TARGET0;
    float Depth : SV_TARGET1;
    float4 WorldNormal : SV_TARGET2;
    float4 SpecularLighting : SV_TARGET3;
    float4 DiffuseLighting : SV_TARGET4;
};

PS_OUTPUT main_ps(VS_OUTPUT input)
{
    PS_OUTPUT output;
    const float2 uvBase = GetTextureCoordinate(g_TextureCoordinates0.x, input);
    const float2 uvNormal = GetTextureCoordinate(g_TextureCoordinates0.y, input);
    const float2 uvMetallicRoughness = GetTextureCoordinate(g_TextureCoordinates0.z, input);
    const float2 uvOcclusion = GetTextureCoordinate(g_TextureCoordinates0.w, input);
    const float2 uvEmissive = GetTextureCoordinate(g_TextureCoordinates1.x, input);
    const float2 baseUv = float2(dot(uvBase, g_TextureTransformRows[0].xy),
                                 dot(uvBase, g_TextureTransformRows[0].zw)) + g_TextureTransformOffsets[0].xy;
    const float2 normalUv = float2(dot(uvNormal, g_TextureTransformRows[1].xy),
                                   dot(uvNormal, g_TextureTransformRows[1].zw)) + g_TextureTransformOffsets[1].xy;
    const float2 mrUv = float2(dot(uvMetallicRoughness, g_TextureTransformRows[2].xy),
                               dot(uvMetallicRoughness, g_TextureTransformRows[2].zw)) + g_TextureTransformOffsets[2].xy;
    const float2 occlusionUv = float2(dot(uvOcclusion, g_TextureTransformRows[3].xy),
                                      dot(uvOcclusion, g_TextureTransformRows[3].zw)) + g_TextureTransformOffsets[3].xy;
    const float2 emissiveUv = float2(dot(uvEmissive, g_TextureTransformRows[4].xy),
                                     dot(uvEmissive, g_TextureTransformRows[4].zw)) + g_TextureTransformOffsets[4].xy;
    const float4 baseColorSample = g_BaseColorMap.Sample(g_BaseColorSampler, baseUv);
    const float4 metallicRoughnessSample = g_MetallicRoughnessMap.Sample(g_MetallicRoughnessSampler, mrUv);
    const float occlusionSample = g_OcclusionMap.Sample(g_OcclusionSampler, occlusionUv).r;
    const float3 emissiveSample = g_EmissiveMap.Sample(g_EmissiveSampler, emissiveUv).rgb;
    const float4 baseColorFactor = g_BaseColor * input.color;
    const float3 baseColor = baseColorFactor.rgb * baseColorSample.rgb;
    const float baseAlpha = baseColorFactor.a * baseColorSample.a;
    if ((g_ModelFlags.w & 0xffu) == 1u && baseAlpha < g_AlphaParams.x)
    {
        discard;
    }

    if (g_ObjectParams.y > 0.5f)
    {
        const float3 unlit = baseColor + g_EmissiveMetallic.rgb * emissiveSample;
        output.FinalColor = float4(unlit, baseAlpha);
        output.Depth = input.pos.z;
        output.WorldNormal = float4(BrdfNormalizeOr(input.normalWS, float3(0.0f, 0.0f, 1.0f)), 1.0f);
        output.SpecularLighting = float4(0.0f, 0.0f, 0.0f, baseAlpha);
        output.DiffuseLighting = float4(unlit, baseAlpha);
        return output;
    }

    const float3 geometricNormal = BrdfNormalizeOr(input.normalWS, float3(0.0f, 0.0f, 1.0f));
    const float3 tangent = BrdfNormalizeOr(input.tangentWS.xyz - geometricNormal * dot(geometricNormal, input.tangentWS.xyz),
        abs(geometricNormal.y) < 0.9f ? normalize(cross(float3(0.0f, 1.0f, 0.0f), geometricNormal)) : float3(1.0f, 0.0f, 0.0f));
    const float3 bitangent = cross(geometricNormal, tangent) * (input.tangentWS.w < 0.0f ? -1.0f : 1.0f);
    float3 tangentNormal = g_NormalMap.Sample(g_NormalSampler, normalUv).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= g_TextureParams.x;
    float3 N = BrdfNormalizeOr(tangent * tangentNormal.x + bitangent * tangentNormal.y + geometricNormal * tangentNormal.z,
        geometricNormal);
    const bool isSpecularGlossiness = (g_ModelFlags.z & 0x100u) != 0u;
    float  metallic = saturate(g_EmissiveMetallic.w * metallicRoughnessSample.b);
    const float roughness = isSpecularGlossiness
        ? saturate(1.0f - (1.0f - g_SurfaceParams.x) * metallicRoughnessSample.a)
        : saturate(g_SurfaceParams.x * metallicRoughnessSample.g);
    const float alpha = max(roughness * roughness, BRDF_MIN_ALPHA);
    const float ior = max(g_SurfaceParams.w, 1.0f);
    const float iorRatio = (ior - 1.0f) / (ior + 1.0f);
    const float3 dielectricF0 = (g_ModelFlags.z & 0xffu) == 1u
        ? saturate(g_SpecularParams.rgb)
        : float3(iorRatio * iorRatio, iorRatio * iorRatio, iorRatio * iorRatio);
    const float3 specularF0 = isSpecularGlossiness
        ? saturate(g_SpecularParams.rgb * metallicRoughnessSample.rgb)
        : dielectricF0;
    const float3 diffuseColor = isSpecularGlossiness
        ? baseColor * (1.0f - max(specularF0.r, max(specularF0.g, specularF0.b)))
        : baseColor * (1.0f - metallic);
    const float3 V = g_CameraPosition.xyz - input.worldPos;

    float3 diffuseAccum = 0.0f;
    float3 specAccum    = 0.0f;

    [loop]
    for (uint i = 0; i < g_LightCount; ++i)
    {
        float3 lightDir   = normalize(g_Lights[i].DirectionIntensity.xyz);
        float  intensity  = g_Lights[i].DirectionIntensity.w;
        float3 lightColor = g_Lights[i].ColorUsePosition.rgb;
        float  usePosition = g_Lights[i].ColorUsePosition.w;
        float  lightType = g_Lights[i].ConeParams.z;

        float3 L            = normalize(-lightDir);
        float  attenuation  = 1.0f;

        if (lightType == 1.0f)
        {
            const float3 lightPosition = g_Lights[i].PositionRange.xyz;
            const float range = max(g_Lights[i].PositionRange.w, 0.001f);
            const float3 toLight = lightPosition - input.worldPos;
            const float distance = length(toLight);
            attenuation = BrdfInverseSquareRangeAttenuation(distance, range);
            if (attenuation <= 0.0f)
            {
                continue;
            }
            L = toLight / max(distance, 0.0001f);
        }
        else if (usePosition > 0.5f)
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


        const BrdfQuery query = BuildBrdfQuery(N, V, L);
        const float3 brdfBaseColor = isSpecularGlossiness ? diffuseColor : baseColor;
        const float brdfMetallic = isSpecularGlossiness ? 0.0f : metallic;
        const float3 brdfF0 = isSpecularGlossiness ? specularF0 : dielectricF0;
        float3 diffuseBrdf;
        float3 specularBrdf;
        if (g_ModelFlags.y == 6u)
        {
            EvaluateAshikhminShirleyBrdfComponents(query, tangent, bitangent, brdfBaseColor, brdfMetallic,
                brdfF0, g_SpecularParams.w, g_AlphaParams.y, diffuseBrdf, specularBrdf);
        }
        else if (g_ModelFlags.y == 2u)
        {
            EvaluateGtrBrdfComponents(query, brdfBaseColor, brdfMetallic, brdfF0, alpha,
                g_SurfaceParams.z, diffuseBrdf, specularBrdf);
        }
        else if (g_ModelFlags.y == 3u)
        {
            EvaluateBeckmannBrdfComponents(query, brdfBaseColor, brdfMetallic, brdfF0, alpha,
                diffuseBrdf, specularBrdf);
        }
        else if (g_ModelFlags.y == 4u)
        {
            EvaluateGaussBrdfComponents(query, brdfBaseColor, brdfMetallic, brdfF0, alpha,
                diffuseBrdf, specularBrdf);
        }
        else if (g_ModelFlags.y == 5u)
        {
            EvaluateBlinnPhongBrdfComponents(query, brdfBaseColor, brdfMetallic, brdfF0, alpha,
                diffuseBrdf, specularBrdf);
        }
        else if (g_ModelFlags.y == 7u)
        {
            EvaluateSchlickBrdfComponents(query, brdfBaseColor, brdfMetallic, brdfF0, alpha,
                diffuseBrdf, specularBrdf);
        }
        else if (g_ModelFlags.y == 8u)
        {
            EvaluateSawickiBrdfComponents(query, brdfBaseColor, brdfMetallic, brdfF0, alpha,
                diffuseBrdf, specularBrdf);
        }
        else
        {
            EvaluateGgxBrdfComponents(query, brdfBaseColor, brdfMetallic, brdfF0, alpha,
                diffuseBrdf, specularBrdf);
        }
        const float3 lightScale = lightColor * (query.NoL * intensity * attenuation * shadow);
        diffuseAccum += diffuseBrdf * lightScale;
        specAccum += specularBrdf * lightScale;

    }

    const float occlusion = lerp(1.0f, occlusionSample, saturate(g_TextureParams.y));
    float3 ambientDiffuse = 0.0f;
    float3 ambientSpecular = 0.0f;
    if (g_LightPadding.x > 0.5f)
    {
        const float3 viewDirection = BrdfNormalizeOr(V, N);
        const float3 reflectionDirection = reflect(-viewDirection, N);
        const float3 irradiance = g_IrradianceMap.SampleLevel(g_IrradianceSampler,
            BrdfEnvironmentUv(N), 0.0f).rgb;
        uint environmentWidth;
        uint environmentHeight;
        uint environmentMipLevels;
        g_PrefilteredEnvironment.GetDimensions(0, environmentWidth, environmentHeight, environmentMipLevels);
        const float environmentMip = saturate(roughness) * float(max(environmentMipLevels, 1u) - 1u);
        const float3 prefiltered = g_PrefilteredEnvironment.SampleLevel(g_PrefilteredEnvironmentSampler,
            BrdfEnvironmentUv(reflectionDirection), environmentMip).rgb;
        const float2 brdfIntegration = g_BrdfIntegrationLut.Sample(g_BrdfIntegrationSampler,
            float2(saturate(dot(N, viewDirection)), saturate(roughness)));
        const float3 iblF0 = isSpecularGlossiness ? specularF0 : lerp(dielectricF0, baseColor, metallic);
        EvaluateSplitSumIblComponents(irradiance, prefiltered, brdfIntegration, N, viewDirection,
            diffuseColor, iblF0, saturate(roughness), occlusion, ambientDiffuse, ambientSpecular);
    }
    float3 emissive = g_EmissiveMetallic.rgb * emissiveSample;

    const float3 diffuseLighting = ambientDiffuse + diffuseAccum + emissive;
    const float3 specularLighting = ambientSpecular + specAccum;
    output.FinalColor = float4(diffuseLighting + specularLighting, baseAlpha);
    output.Depth = input.pos.z;
    output.WorldNormal = float4(N, 1.0f);
    output.SpecularLighting = float4(specularLighting, baseAlpha);
    output.DiffuseLighting = float4(diffuseLighting, baseAlpha);
    return output;
}
