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

Texture2D<float4> g_BaseColorMap : register(t0);
SamplerState g_BaseColorSampler : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 normalWS : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float2 uv : TEXCOORD2;
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

float2 GetTextureCoordinate(uint index, PS_INPUT input)
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

void main_ps(PS_INPUT input)
{
    const float2 uv = GetTextureCoordinate(g_TextureCoordinates0.x, input);
    const float2 transformedUv = float2(dot(uv, g_TextureTransformRows[0].xy),
                                        dot(uv, g_TextureTransformRows[0].zw)) +
                                 g_TextureTransformOffsets[0].xy;
    const float alpha = g_BaseColor.a * input.color.a * g_BaseColorMap.Sample(g_BaseColorSampler, transformedUv).a;
    clip(alpha - g_AlphaParams.x);
}
