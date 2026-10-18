// Maps linear scene-referred color to the display encoding used by PNG output.
float3 ApplyDisplayTransform(float3 linearColor)
{
    const float3 nonnegative = max(linearColor, 0.0f);
    const float3 reinhard = nonnegative / (1.0f + nonnegative);
    const float3 low = 12.92f * reinhard;
    const float3 high = 1.055f * pow(reinhard, 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, step(reinhard, 0.0031308f));
}
