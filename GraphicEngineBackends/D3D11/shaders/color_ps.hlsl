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
    float4 g_NdfCoefficients[12];
    float4 g_NdfParams;
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
    uint      g_EnvironmentEnabled;
    float     g_EnvironmentIntensity;
    float     g_AmbientLightIntensity;
    float4    g_AmbientLightColor;
};

cbuffer CameraCB : register(b4)
{
    float4 g_CameraPosition;
    float4 g_AmbientOcclusionParams;
};

Texture2DArray<float>  g_ShadowMaps    : register(t6);
SamplerComparisonState g_ShadowSampler : register(s6);
Texture2D<float4> g_IrradianceMap : register(t7);
SamplerState g_IrradianceSampler : register(s7);
Texture2D<float4> g_PrefilteredEnvironment : register(t8);
SamplerState g_PrefilteredEnvironmentSampler : register(s8);
Texture2D<float2> g_BrdfIntegrationLut : register(t9);
SamplerState g_BrdfIntegrationSampler : register(s9);
Texture2D<float> g_AmbientOcclusionTexture : register(t10);
Texture2D<float> g_NdfResidual : register(t3);
SamplerState g_NdfResidualSampler : register(s3);
Texture2D<float4> g_EnvironmentPanorama : register(t11);
SamplerState g_EnvironmentPanoramaSampler : register(s11);
Texture2D<float> g_NdfDistribution : register(t12);
SamplerState g_NdfDistributionSampler : register(s12);

float3 EvaluatePrismNdfEnvironmentSpecular(float3 normal, float3 viewDirection, float3 tangent,
    float3 bitangent, float3 f0, float alpha, float2 pixelPosition)
{
    static const uint SAMPLE_COUNT = 128u;
    static const float GOLDEN_COS = -0.737368878f;
    static const float GOLDEN_SIN = 0.675490294f;
    const float noV = max(dot(normal, viewDirection), BRDF_EPSILON);
    const float3 localTangent = BrdfNormalizeOr(tangent - normal * dot(normal, tangent),
        abs(normal.y) < 0.9f ? normalize(cross(float3(0.0f, 1.0f, 0.0f), normal)) : float3(1.0f, 0.0f, 0.0f));
    const float handedness = dot(cross(normal, localTangent), bitangent) < 0.0f ? -1.0f : 1.0f;
    const float3 localBitangent = cross(normal, localTangent) * handedness;
    const float phase = frac(sin(dot(pixelPosition, float2(12.9898f, 78.233f))) * 43758.5453f) * 2.0f * BRDF_PI;
    float2 rotation = float2(cos(phase), sin(phase));
    float2 golden = float2(GOLDEN_COS, GOLDEN_SIN);
    float3 accumulated = 0.0f;
    [loop]
    for (uint index = 0u; index < SAMPLE_COUNT; ++index)
    {
        const float radius = sqrt((float(index) + 0.5f) / float(SAMPLE_COUNT));
        const float2 disk = rotation * radius;
        const float2 uv = disk * 0.5f + 0.5f;
        const float distribution = g_NdfDistribution.SampleLevel(g_NdfDistributionSampler, uv, 0.0f);
        const float noH = sqrt(max(1.0f - dot(disk, disk), 0.0f));
        const float3 halfVector = BrdfNormalizeOr(localTangent * disk.x + localBitangent * disk.y + normal * noH, normal);
        const float voH = dot(viewDirection, halfVector);
        if (distribution > 0.0f && noH > BRDF_EPSILON && voH > 0.0f)
        {
            const float3 lightDirection = 2.0f * voH * halfVector - viewDirection;
            const float noL = dot(normal, lightDirection);
            if (noL > BRDF_EPSILON)
            {
                const float geometry = BrdfSmithGgxCorrelated(noV, noL, alpha);
                const float3 fresnel = BrdfFresnelSchlick(f0, voH);
                const float3 radiance = g_EnvironmentPanorama.SampleLevel(
                    g_EnvironmentPanoramaSampler, BrdfEnvironmentUv(lightDirection), 0.0f).rgb;
                accumulated += radiance * fresnel * (distribution * geometry * voH / (noV * noH));
            }
        }
        rotation = float2(rotation.x * golden.x - rotation.y * golden.y,
                          rotation.x * golden.y + rotation.y * golden.x);
    }
    return accumulated * (BRDF_PI / float(SAMPLE_COUNT));
}

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
    const float4 baseColor = g_BaseColor * input.color;
    if ((g_ModelFlags.w & 0xffu) == 1u && baseColor.a < g_AlphaParams.x)
    {
        discard;
    }

    if (g_ObjectParams.y > 0.5f)
    {
        const float3 unlit = baseColor.rgb + g_EmissiveMetallic.rgb;
        output.FinalColor = float4(unlit, baseColor.a);
        output.Depth = input.pos.z;
        output.WorldNormal = float4(BrdfNormalizeOr(input.normalWS, float3(0.0f, 0.0f, 1.0f)), 1.0f);
        output.SpecularLighting = float4(0.0f, 0.0f, 0.0f, baseColor.a);
        output.DiffuseLighting = float4(unlit, baseColor.a);
        return output;
    }

    float3 N        = BrdfNormalizeOr(input.normalWS, float3(0.0f, 0.0f, 1.0f));
    const float3 tangent = BrdfNormalizeOr(input.tangentWS.xyz - N * dot(N, input.tangentWS.xyz),
        abs(N.y) < 0.9f ? normalize(cross(float3(0.0f, 1.0f, 0.0f), N)) : float3(1.0f, 0.0f, 0.0f));
    const float3 bitangent = cross(N, tangent) * (input.tangentWS.w < 0.0f ? -1.0f : 1.0f);
    float  metallic = saturate(g_EmissiveMetallic.w);
    const bool isSpecularGlossiness = (g_ModelFlags.z & 0x100u) != 0u;
    const float roughness = isSpecularGlossiness ? 1.0f - g_SurfaceParams.x : g_SurfaceParams.x;
    const float alpha = max(roughness * roughness, BRDF_MIN_ALPHA);
    const float ior = max(g_SurfaceParams.w, 1.0f);
    const float iorRatio = (ior - 1.0f) / (ior + 1.0f);
    const float3 dielectricF0 = (g_ModelFlags.z & 0xffu) == 1u
        ? saturate(g_SpecularParams.rgb)
        : float3(iorRatio * iorRatio, iorRatio * iorRatio, iorRatio * iorRatio);
    const float3 specularF0 = saturate(g_SpecularParams.rgb);
    const float3 diffuseColor = isSpecularGlossiness
        ? baseColor.rgb * (1.0f - max(specularF0.r, max(specularF0.g, specularF0.b)))
        : baseColor.rgb * (1.0f - metallic);
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
        const float3 brdfBaseColor = isSpecularGlossiness ? diffuseColor : baseColor.rgb;
        const float brdfMetallic = isSpecularGlossiness ? 0.0f : metallic;
        const float3 brdfF0 = isSpecularGlossiness ? specularF0 : dielectricF0;
        float3 diffuseBrdf;
        float3 specularBrdf;
        if (g_ModelFlags.y == 9u)
        {
            const float3 ndfTangent = BrdfNormalizeOr(tangent - N * dot(N, tangent), tangent);
            const float ndfHandedness = dot(cross(N, ndfTangent), bitangent) < 0.0f ? -1.0f : 1.0f;
            const float3 ndfBitangent = cross(N, ndfTangent) * ndfHandedness;
            const float2 ndfCoord = float2(dot(query.H, ndfTangent), dot(query.H, ndfBitangent));
            const float residual = g_NdfResidual.SampleLevel(g_NdfResidualSampler, ndfCoord * 0.5f + 0.5f, 0.0f);
            const float distribution = PrismZernikeNdf(ndfCoord, g_NdfCoefficients, residual, g_NdfParams.x);
            EvaluateMicrofacetBrdfComponents(query, brdfBaseColor, brdfMetallic, brdfF0, alpha,
                distribution, diffuseBrdf, specularBrdf);
        }
        else if (g_ModelFlags.y == 6u)
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

    float3 ambientDiffuse = 0.0f;
    float3 ambientSpecular = 0.0f;
    if (g_EnvironmentEnabled != 0)
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
        const float3 iblF0 = isSpecularGlossiness ? specularF0 : lerp(dielectricF0, baseColor.rgb, metallic);
        if (g_ModelFlags.y == 9u)
        {
            ambientDiffuse = irradiance * diffuseColor * (1.0f - BrdfFresnelSchlickRoughness(
                iblF0, saturate(dot(N, viewDirection)), saturate(roughness))) / BRDF_PI;
            ambientSpecular = EvaluatePrismNdfEnvironmentSpecular(N, viewDirection, tangent, bitangent,
                iblF0, alpha, input.pos.xy);
        }
        else
        {
            EvaluateSplitSumIblComponents(irradiance, prefiltered, brdfIntegration, N, viewDirection,
                diffuseColor, iblF0, saturate(roughness), 1.0f, ambientDiffuse, ambientSpecular);
        }
    }
    float3 sceneAmbientDiffuse = diffuseColor * g_AmbientLightColor.rgb * max(g_AmbientLightIntensity, 0.0f);
    float3 emissive = g_EmissiveMetallic.rgb;

    if (g_AmbientOcclusionParams.x > 0.5f)
    {
        const float ambientOcclusion = g_AmbientOcclusionTexture.Load(int3(int2(input.pos.xy), 0));
        ambientDiffuse *= ambientOcclusion;
        sceneAmbientDiffuse *= ambientOcclusion;
    }
    const float3 diffuseLighting = (ambientDiffuse * max(g_EnvironmentIntensity, 0.0f)) + sceneAmbientDiffuse +
                                   diffuseAccum + emissive;
    const float3 specularLighting = (ambientSpecular * max(g_EnvironmentIntensity, 0.0f)) + specAccum;
    output.FinalColor = float4(diffuseLighting + specularLighting, baseColor.a);
    output.Depth = input.pos.z;
    output.WorldNormal = float4(N, 1.0f);
    output.SpecularLighting = float4(specularLighting, baseColor.a);
    output.DiffuseLighting = float4(diffuseLighting, baseColor.a);
    return output;
}
