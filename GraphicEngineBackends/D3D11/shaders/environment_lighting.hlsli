#ifndef ENGINE_ENVIRONMENT_LIGHTING_HLSLI
#define ENGINE_ENVIRONMENT_LIGHTING_HLSLI

// Samples an anisotropic GGX proposal in the local shading frame. The proposal
// has full support over the visible half-vector hemisphere and remains useful
// for the narrowest supported analytic NDFs; the selected NDF/PDF ratio below
// keeps the estimate unbiased for every analytic distribution.
float3 BrdfSampleAnisotropicGgx(float2 sample, float alphaU, float alphaV)
{
    const float phi = 2.0f * BRDF_PI * sample.y;
    const float cosPhi = cos(phi);
    const float sinPhi = sin(phi);
    const float warpedPhi = atan2(alphaV * sinPhi, alphaU * cosPhi);
    const float warpedCos = cos(warpedPhi);
    const float warpedSin = sin(warpedPhi);
    const float effectiveAlphaSquared = rcp(warpedCos * warpedCos / (alphaU * alphaU) +
        warpedSin * warpedSin / (alphaV * alphaV));
    const float safeU = min(sample.x, 1.0f - 1.0e-6f);
    const float tanThetaSquared = effectiveAlphaSquared * safeU / max(1.0f - safeU, 1.0e-6f);
    const float cosTheta = rsqrt(1.0f + tanThetaSquared);
    const float sinTheta = sqrt(max(tanThetaSquared, 0.0f)) * cosTheta;
    return float3(sinTheta * warpedCos, sinTheta * warpedSin, cosTheta);
}

float3 BrdfSampleUniformProjectedDisk(float2 sample)
{
    const float radius = sqrt(sample.x);
    const float phi = 2.0f * BRDF_PI * sample.y;
    return float3(radius * cos(phi), radius * sin(phi), sqrt(max(1.0f - sample.x, 0.0f)));
}

float3 BrdfSampleGtr(float2 sample, float alpha, float gamma)
{
    const float safeAlpha = max(alpha, BRDF_MIN_ALPHA);
    const float safeGamma = clamp(gamma, BRDF_MIN_GTR_GAMMA, BRDF_MAX_GTR_GAMMA);
    const float alphaSquared = safeAlpha * safeAlpha;
    const float u = min(sample.x, 1.0f - 1.0e-6f);
    float tanThetaSquared;
    if (abs(alphaSquared - 1.0f) <= 1.0e-5f)
    {
        tanThetaSquared = u / max(1.0f - u, 1.0e-6f);
    }
    else
    {
        float logT;
        if (abs(safeGamma - 1.0f) <= 1.0e-4f)
        {
            logT = (1.0f - u) * log(alphaSquared);
        }
        else
        {
            const float exponent = (1.0f - safeGamma) * log(alphaSquared);
            const float logInterpolatedPower = abs(exponent) <= 1.0e-2f
                ? BrdfLog1pStable((1.0f - u) * BrdfExpm1Stable(exponent))
                : exponent + log((1.0f - u) + u * exp(-exponent));
            logT = logInterpolatedPower / (1.0f - safeGamma);
        }
        const float t = exp(logT);
        tanThetaSquared = max((alphaSquared - t) / (t - 1.0f), 0.0f);
    }
    const float cosTheta = rsqrt(1.0f + tanThetaSquared);
    const float stableSinTheta = sqrt(max(tanThetaSquared, 0.0f)) * cosTheta;
    const float phi = 2.0f * BRDF_PI * sample.y;
    return float3(stableSinTheta * cos(phi), stableSinTheta * sin(phi), cosTheta);
}

float BrdfAnisotropicGgxNdf(float3 halfTangent, float alphaU, float alphaV)
{
    const float safeU = max(alphaU, BRDF_MIN_ALPHA);
    const float safeV = max(alphaV, BRDF_MIN_ALPHA);
    const float scaledX = halfTangent.x / safeU;
    const float scaledY = halfTangent.y / safeV;
    const float denominator = scaledX * scaledX + scaledY * scaledY + halfTangent.z * halfTangent.z;
    return rcp(BRDF_PI * safeU * safeV * max(denominator * denominator, BRDF_EPSILON));
}

float BrdfRadicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

float3 EvaluateAnalyticNdfEnvironmentSpecular(uint model, float3 normal, float3 viewDirection,
    float3 tangent, float3 bitangent, float3 f0, float roughness, float roughnessU, float roughnessV,
    float gamma, float2 pixelPosition)
{
    static const uint SAMPLE_COUNT = 128u;
    const float noV = dot(normal, viewDirection);
    if (noV <= BRDF_EPSILON) return 0.0f;

    const float3 localTangent = BrdfTangentForNormal(normal, tangent);
    const float handedness = dot(cross(normal, localTangent), bitangent) < 0.0f ? -1.0f : 1.0f;
    const float3 localBitangent = cross(normal, localTangent) * handedness;
    const float alpha = max(roughness * roughness, BRDF_MIN_ALPHA);
    const bool anisotropic = model == 6u;
    const float alphaU = anisotropic ? max(roughnessU * roughnessU, BRDF_MIN_ALPHA) : alpha;
    const float alphaV = anisotropic ? max(roughnessV * roughnessV, BRDF_MIN_ALPHA) : alpha;
    const float geometryAlpha = anisotropic ? max(alphaU, alphaV) : alpha;
    const float phase = frac(sin(dot(pixelPosition, float2(12.9898f, 78.233f))) * 43758.5453f);
    float3 accumulated = 0.0f;
    static const float UNIFORM_TAIL_WEIGHT = 0.125f;

    [loop]
    for (uint index = 0u; index < SAMPLE_COUNT; ++index)
    {
        const float2 sample = float2((float(index) + 0.5f) / float(SAMPLE_COUNT),
            frac(BrdfRadicalInverse(index) + phase));
        const bool useGtrProposal = model == 2u;
        float3 halfTangent;
        if (useGtrProposal)
        {
            halfTangent = BrdfSampleGtr(sample, alpha, gamma);
        }
        else if (sample.x < UNIFORM_TAIL_WEIGHT)
        {
            halfTangent = BrdfSampleUniformProjectedDisk(
                float2(sample.x / UNIFORM_TAIL_WEIGHT, sample.y));
        }
        else
        {
            halfTangent = BrdfSampleAnisotropicGgx(
                float2((sample.x - UNIFORM_TAIL_WEIGHT) / (1.0f - UNIFORM_TAIL_WEIGHT), sample.y), alphaU, alphaV);
        }
        const float noH = halfTangent.z;
        const float3 halfVector = BrdfNormalizeOr(localTangent * halfTangent.x +
            localBitangent * halfTangent.y + normal * noH, normal);
        const float voH = dot(viewDirection, halfVector);
        if (voH > BRDF_EPSILON && noH > BRDF_EPSILON)
        {
            const float3 lightDirection = 2.0f * voH * halfVector - viewDirection;
            const float noL = dot(normal, lightDirection);
            if (noL > BRDF_EPSILON)
            {
                const float distribution = BrdfSelectedNdf(model, noH, halfTangent, alpha,
                    roughnessU, roughnessV, gamma);
                const float ggxDensity = BrdfAnisotropicGgxNdf(halfTangent, alphaU, alphaV) * noH;
                const float proposalDensity = useGtrProposal
                    ? BrdfGtrNdf(noH, alpha, gamma) * noH
                    : lerp(ggxDensity, noH / BRDF_PI, UNIFORM_TAIL_WEIGHT);
                if (distribution > 0.0f && proposalDensity > BRDF_EPSILON)
                {
                    const float geometry = BrdfSmithGgxCorrelated(noV, noL, geometryAlpha);
                    const float3 fresnel = BrdfFresnelSchlick(f0, voH);
                    const float3 radiance = g_EnvironmentPanorama.SampleLevel(
                        g_EnvironmentPanoramaSampler, BrdfEnvironmentUv(lightDirection), 0.0f).rgb;
                    accumulated += radiance * fresnel * (distribution * geometry * voH / (noV * proposalDensity));
                }
            }
        }
    }
    return accumulated / float(SAMPLE_COUNT);
}

#endif
