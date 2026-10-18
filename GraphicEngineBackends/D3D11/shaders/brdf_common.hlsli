#ifndef ENGINE_BRDF_COMMON_HLSLI
#define ENGINE_BRDF_COMMON_HLSLI

static const float BRDF_PI = 3.14159265358979323846f;
static const float BRDF_EPSILON = 1.0e-6f;
static const float BRDF_MIN_ALPHA = 1.0e-3f;
static const float BRDF_MIN_GTR_GAMMA = 1.0f;
static const float BRDF_MAX_GTR_GAMMA = 8.0f;

struct BrdfQuery
{
    float3 N;
    float3 V;
    float3 L;
    float3 H;
    float NoV;
    float NoL;
    float NoH;
    float VoH;
};

float3 BrdfNormalizeOr(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > BRDF_EPSILON ? value * rsqrt(lengthSquared) : fallback;
}

BrdfQuery BuildBrdfQuery(float3 normal, float3 view, float3 light)
{
    BrdfQuery query;
    query.N = BrdfNormalizeOr(normal, float3(0.0f, 0.0f, 1.0f));
    query.V = BrdfNormalizeOr(view, query.N);
    query.L = BrdfNormalizeOr(light, query.N);
    query.H = BrdfNormalizeOr(query.V + query.L, query.N);
    query.NoV = saturate(dot(query.N, query.V));
    query.NoL = saturate(dot(query.N, query.L));
    query.NoH = saturate(dot(query.N, query.H));
    query.VoH = saturate(dot(query.V, query.H));
    return query;
}

float3 BrdfFresnelSchlick(float3 f0, float voH)
{
    const float fresnelWeight = pow(1.0f - saturate(voH), 5.0f);
    return f0 + (1.0f - f0) * fresnelWeight;
}

float3 BrdfFresnelSchlickRoughness(float3 f0, float noV, float roughness)
{
    const float3 grazing = max(1.0f - roughness, f0);
    const float fresnelWeight = pow(1.0f - saturate(noV), 5.0f);
    return f0 + (grazing - f0) * fresnelWeight;
}

float2 BrdfEnvironmentUv(float3 direction)
{
    const float3 safeDirection = BrdfNormalizeOr(direction, float3(0.0f, 1.0f, 0.0f));
    const float longitude = atan2(safeDirection.z, safeDirection.x) / (2.0f * BRDF_PI) + 0.5f;
    const float latitude = acos(clamp(safeDirection.y, -1.0f, 1.0f)) / BRDF_PI;
    return float2(frac(longitude), latitude);
}

float3 EvaluateSplitSumIbl(float3 diffuseIrradiance, float3 prefilteredRadiance, float2 brdfIntegration,
    float3 normal, float3 view, float3 diffuseColor, float3 f0, float roughness, float occlusion)
{
    const float noV = saturate(dot(normal, view));
    const float3 fresnel = BrdfFresnelSchlickRoughness(f0, noV, roughness);
    const float3 diffuse = diffuseIrradiance * diffuseColor * (1.0f - fresnel) / BRDF_PI;
    const float3 specular = prefilteredRadiance * (fresnel * brdfIntegration.x + brdfIntegration.y);
    return (diffuse + specular) * saturate(occlusion);
}

void EvaluateSplitSumIblComponents(float3 diffuseIrradiance, float3 prefilteredRadiance, float2 brdfIntegration,
    float3 normal, float3 view, float3 diffuseColor, float3 f0, float roughness, float occlusion,
    out float3 diffuse, out float3 specular)
{
    const float noV = saturate(dot(normal, view));
    const float3 fresnel = BrdfFresnelSchlickRoughness(f0, noV, roughness);
    diffuse = diffuseIrradiance * diffuseColor * (1.0f - fresnel) / BRDF_PI * saturate(occlusion);
    specular = prefilteredRadiance * (fresnel * brdfIntegration.x + brdfIntegration.y) * saturate(occlusion);
}

float BrdfSmithGgxCorrelated(float noV, float noL, float alpha)
{
    const float safeNoV = saturate(noV);
    const float safeNoL = saturate(noL);
    const float safeAlpha = max(alpha, BRDF_MIN_ALPHA);
    const float alphaSquared = safeAlpha * safeAlpha;
    const float smithV = safeNoL * sqrt(max(alphaSquared + (1.0f - alphaSquared) * safeNoV * safeNoV, 0.0f));
    const float smithL = safeNoV * sqrt(max(alphaSquared + (1.0f - alphaSquared) * safeNoL * safeNoL, 0.0f));
    const float denominator = smithV + smithL;
    return denominator > BRDF_EPSILON ? (2.0f * safeNoV * safeNoL) / denominator : 0.0f;
}

float3 BrdfLambert(float3 diffuseColor)
{
    return diffuseColor / BRDF_PI;
}

float BrdfInverseSquareRangeAttenuation(float distance, float range)
{
    const float safeRange = max(range, BRDF_EPSILON);
    const float normalizedDistance = saturate(distance / safeRange);
    const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance *
        normalizedDistance * normalizedDistance);
    return distance < safeRange
        ? rangeFade * rangeFade / max(distance * distance, 1.0e-4f)
        : 0.0f;
}

float BrdfGgxNdf(float noH, float alpha)
{
    const float safeNoH = saturate(noH);
    const float safeAlpha = max(alpha, BRDF_MIN_ALPHA);
    const float alphaSquared = safeAlpha * safeAlpha;
    const float denominator = 1.0f + (alphaSquared - 1.0f) * safeNoH * safeNoH;
    const float safeDenominator = max(denominator, BRDF_EPSILON);
    return alphaSquared / (BRDF_PI * safeDenominator * safeDenominator);
}

float BrdfBeckmannNdf(float noH, float alpha)
{
    const float safeNoH = saturate(noH);
    const float noHSquared = safeNoH * safeNoH;
    if (noHSquared <= BRDF_EPSILON)
    {
        return 0.0f;
    }

    const float safeAlpha = max(alpha, BRDF_MIN_ALPHA);
    const float alphaSquared = safeAlpha * safeAlpha;
    const float denominator = max(BRDF_PI * alphaSquared * noHSquared * noHSquared, BRDF_EPSILON);
    const float exponent = (noHSquared - 1.0f) / (alphaSquared * noHSquared);
    return exp(exponent) / denominator;
}

float BrdfGaussNdf(float noH, float alpha)
{
    const float safeNoH = saturate(noH);
    const float noHSquared = safeNoH * safeNoH;
    if (safeNoH <= 0.0f)
    {
        return 0.0f;
    }
    const float safeAlpha = max(alpha, BRDF_MIN_ALPHA);
    const float alphaSquared = safeAlpha * safeAlpha;
    const float tanSquared = (1.0f - noHSquared) / max(noHSquared, BRDF_EPSILON);
    const float exponent = -tanSquared / (2.0f * alphaSquared);
    const float cosFourth = noHSquared * noHSquared;
    const float denominator = max(2.0f * BRDF_PI * alphaSquared * cosFourth, BRDF_EPSILON);
    return exp(exponent) / denominator;
}

float BrdfBlinnPhongNdf(float noH, float alpha)
{
    const float safeNoH = saturate(noH);
    const float safeAlpha = max(alpha, BRDF_MIN_ALPHA);
    const float exponent = max(2.0f / (safeAlpha * safeAlpha) - 2.0f, 0.0f);
    const float cosinePower = safeNoH == 0.0f && exponent == 0.0f ? 1.0f : pow(safeNoH, exponent);
    return ((exponent + 2.0f) / (2.0f * BRDF_PI)) * cosinePower;
}

float BrdfSchlickNdf(float noH, float alpha)
{
    const float safeNoH = saturate(noH);
    const float m = clamp(alpha, BRDF_MIN_ALPHA, 1.0f);
    const float x = safeNoH + m - 1.0f;
    if (safeNoH <= 0.0f || x <= 0.0f)
    {
        return 0.0f;
    }
    const float inner = (m - 1.0f) * x * x + m * m;
    const float denominator = BRDF_PI * safeNoH * inner * inner;
    return (m * m * m * x) / max(denominator, 1.0e-30f);
}

float BrdfAshikhminShirleyNdf(float3 halfTangent, float roughnessU, float roughnessV)
{
    const float noH = saturate(halfTangent.z);
    if (noH <= 0.0f) return 0.0f;
    const float alphaU = max(roughnessU * roughnessU, BRDF_MIN_ALPHA);
    const float alphaV = max(roughnessV * roughnessV, BRDF_MIN_ALPHA);
    const float nu = max(2.0f / (alphaU * alphaU) - 2.0f, 0.0f);
    const float nv = max(2.0f / (alphaV * alphaV) - 2.0f, 0.0f);
    const float tangentSquared = dot(halfTangent.xy, halfTangent.xy);
    const float exponent = tangentSquared > BRDF_EPSILON
        ? (nu * halfTangent.x * halfTangent.x + nv * halfTangent.y * halfTangent.y) / tangentSquared
        : 0.5f * (nu + nv);
    const float denominator = max(2.0f * BRDF_PI * noH * noH * noH * noH, BRDF_EPSILON);
    return sqrt((nu + 1.0f) * (nv + 1.0f)) * pow(noH, exponent) / denominator;
}

float BrdfGtrLogNormalization(float alpha, float gamma)
{
    const float safeAlpha = max(alpha, BRDF_MIN_ALPHA);
    const float safeGamma = clamp(gamma, BRDF_MIN_GTR_GAMMA, BRDF_MAX_GTR_GAMMA);
    const float alphaSquared = safeAlpha * safeAlpha;
    if (abs(alphaSquared - 1.0f) <= 1.0e-4f)
    {
        return -log(BRDF_PI);
    }

    const float logAlphaSquared = log(alphaSquared);
    if (abs(safeGamma - 1.0f) <= 1.0e-4f)
    {
        return log(abs(alphaSquared - 1.0f)) - log(BRDF_PI) - log(abs(logAlphaSquared));
    }

    const float exponent = (1.0f - safeGamma) * logAlphaSquared;
    if (exponent > 0.0f)
    {
        const float inverseExponential = exp(-exponent);
        return log(safeGamma - 1.0f) + log(1.0f - alphaSquared) - exponent - log(BRDF_PI) -
            log(1.0f - inverseExponential);
    }

    return log(safeGamma - 1.0f) + log(alphaSquared - 1.0f) - log(BRDF_PI) - log(1.0f - exp(exponent));
}

float BrdfGtrNdf(float noH, float alpha, float gamma)
{
    const float safeNoH = saturate(noH);
    const float safeAlpha = max(alpha, BRDF_MIN_ALPHA);
    const float safeGamma = clamp(gamma, BRDF_MIN_GTR_GAMMA, BRDF_MAX_GTR_GAMMA);
    const float alphaSquared = safeAlpha * safeAlpha;
    const float denominator = max(1.0f + (alphaSquared - 1.0f) * safeNoH * safeNoH, BRDF_EPSILON);
    return exp(BrdfGtrLogNormalization(safeAlpha, safeGamma) - safeGamma * log(denominator));
}

void EvaluateMicrofacetBrdfComponents(BrdfQuery query, float3 baseColor, float metallic, float3 dielectricF0,
    float alpha, float distribution, out float3 diffuse, out float3 specular)
{
    if (query.NoV <= BRDF_EPSILON || query.NoL <= BRDF_EPSILON)
    {
        diffuse = 0.0f;
        specular = 0.0f;
        return;
    }

    const float3 f0 = lerp(dielectricF0, baseColor, saturate(metallic));
    const float3 fresnel = BrdfFresnelSchlick(f0, query.VoH);
    const float geometry = BrdfSmithGgxCorrelated(query.NoV, query.NoL, alpha);
    const float specularDenominator = max(4.0f * query.NoV * query.NoL, BRDF_EPSILON);
    const float3 diffuseColor = baseColor * (1.0f - saturate(metallic));
    diffuse = BrdfLambert(diffuseColor * (1.0f - fresnel));
    specular = fresnel * (distribution * geometry / specularDenominator);
}

float3 EvaluateGgxBrdf(BrdfQuery query, float3 baseColor, float metallic, float3 dielectricF0, float alpha)
{
    float3 diffuse;
    float3 specular;
    EvaluateMicrofacetBrdfComponents(query, baseColor, metallic, dielectricF0, alpha,
        BrdfGgxNdf(query.NoH, alpha), diffuse, specular);
    return diffuse + specular;
}

void EvaluateGgxBrdfComponents(BrdfQuery query, float3 baseColor, float metallic, float3 dielectricF0,
    float alpha, out float3 diffuse, out float3 specular)
{
    EvaluateMicrofacetBrdfComponents(query, baseColor, metallic, dielectricF0, alpha,
        BrdfGgxNdf(query.NoH, alpha), diffuse, specular);
}

float3 EvaluateGtrBrdf(BrdfQuery query, float3 baseColor, float metallic, float3 dielectricF0,
    float alpha, float gamma)
{
    float3 diffuse;
    float3 specular;
    EvaluateMicrofacetBrdfComponents(query, baseColor, metallic, dielectricF0, alpha,
        BrdfGtrNdf(query.NoH, alpha, gamma), diffuse, specular);
    return diffuse + specular;
}

void EvaluateGtrBrdfComponents(BrdfQuery query, float3 baseColor, float metallic, float3 dielectricF0,
    float alpha, float gamma, out float3 diffuse, out float3 specular)
{
    EvaluateMicrofacetBrdfComponents(query, baseColor, metallic, dielectricF0, alpha,
        BrdfGtrNdf(query.NoH, alpha, gamma), diffuse, specular);
}

void EvaluateBeckmannBrdfComponents(BrdfQuery query, float3 baseColor, float metallic, float3 dielectricF0,
    float alpha, out float3 diffuse, out float3 specular)
{
    EvaluateMicrofacetBrdfComponents(query, baseColor, metallic, dielectricF0, alpha,
        BrdfBeckmannNdf(query.NoH, alpha), diffuse, specular);
}

void EvaluateGaussBrdfComponents(BrdfQuery query, float3 baseColor, float metallic, float3 dielectricF0,
    float alpha, out float3 diffuse, out float3 specular)
{
    EvaluateMicrofacetBrdfComponents(query, baseColor, metallic, dielectricF0, alpha,
        BrdfGaussNdf(query.NoH, alpha), diffuse, specular);
}

void EvaluateBlinnPhongBrdfComponents(BrdfQuery query, float3 baseColor, float metallic, float3 dielectricF0,
    float alpha, out float3 diffuse, out float3 specular)
{
    EvaluateMicrofacetBrdfComponents(query, baseColor, metallic, dielectricF0, alpha,
        BrdfBlinnPhongNdf(query.NoH, alpha), diffuse, specular);
}

void EvaluateSchlickBrdfComponents(BrdfQuery query, float3 baseColor, float metallic, float3 dielectricF0,
    float alpha, out float3 diffuse, out float3 specular)
{
    const float3 diffuseColor = baseColor * (1.0f - saturate(metallic));
    const float3 f0 = lerp(dielectricF0, baseColor, saturate(metallic));
    const float3 fresnel = BrdfFresnelSchlick(f0, query.VoH);
    const float geometry = BrdfSmithGgxCorrelated(query.NoV, query.NoL, alpha);
    diffuse = BrdfLambert(diffuseColor * (1.0f - fresnel));
    specular = fresnel * (BrdfSchlickNdf(query.NoH, alpha) * geometry /
        max(4.0f * query.NoV * query.NoL, BRDF_EPSILON));
}

void EvaluateAshikhminShirleyBrdfComponents(BrdfQuery query, float3 tangent, float3 bitangent,
    float3 baseColor, float metallic, float3 dielectricF0, float roughnessU, float roughnessV,
    out float3 diffuse, out float3 specular)
{
    if (query.NoV <= BRDF_EPSILON || query.NoL <= BRDF_EPSILON)
    {
        diffuse = 0.0f;
        specular = 0.0f;
        return;
    }
    const float3 f0 = lerp(dielectricF0, baseColor, saturate(metallic));
    const float3 fresnel = BrdfFresnelSchlick(f0, query.VoH);
    const float alpha = max(max(roughnessU * roughnessU, roughnessV * roughnessV), BRDF_MIN_ALPHA);
    const float geometry = BrdfSmithGgxCorrelated(query.NoV, query.NoL, alpha);
    const float3 halfTangent = float3(dot(query.H, tangent), dot(query.H, bitangent), dot(query.H, query.N));
    const float distribution = BrdfAshikhminShirleyNdf(halfTangent, roughnessU, roughnessV);
    diffuse = BrdfLambert(baseColor * (1.0f - saturate(metallic)) * (1.0f - fresnel));
    specular = fresnel * (distribution * geometry / max(4.0f * query.NoV * query.NoL, BRDF_EPSILON));
}

#endif
