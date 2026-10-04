#ifndef ENGINE_BRDF_COMMON_HLSLI
#define ENGINE_BRDF_COMMON_HLSLI

static const float BRDF_PI = 3.14159265358979323846f;
static const float BRDF_EPSILON = 1.0e-6f;
static const float BRDF_MIN_ALPHA = 1.0e-3f;

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

float BrdfGgxNdf(float noH, float alpha)
{
    const float safeNoH = saturate(noH);
    const float safeAlpha = max(alpha, BRDF_MIN_ALPHA);
    const float alphaSquared = safeAlpha * safeAlpha;
    const float denominator = 1.0f + (alphaSquared - 1.0f) * safeNoH * safeNoH;
    return alphaSquared / (BRDF_PI * max(denominator * denominator, BRDF_EPSILON));
}

float3 EvaluateGgxBrdf(BrdfQuery query, float3 baseColor, float metallic, float3 dielectricF0, float alpha)
{
    if (query.NoV <= BRDF_EPSILON || query.NoL <= BRDF_EPSILON)
    {
        return 0.0f;
    }

    const float3 f0 = lerp(dielectricF0, baseColor, saturate(metallic));
    const float3 fresnel = BrdfFresnelSchlick(f0, query.VoH);
    const float distribution = BrdfGgxNdf(query.NoH, alpha);
    const float geometry = BrdfSmithGgxCorrelated(query.NoV, query.NoL, alpha);
    const float specularDenominator = max(4.0f * query.NoV * query.NoL, BRDF_EPSILON);
    const float3 diffuseColor = baseColor * (1.0f - saturate(metallic));
    const float3 diffuse = BrdfLambert(diffuseColor * (1.0f - fresnel));
    const float3 specular = fresnel * (distribution * geometry / specularDenominator);
    return diffuse + specular;
}

#endif
