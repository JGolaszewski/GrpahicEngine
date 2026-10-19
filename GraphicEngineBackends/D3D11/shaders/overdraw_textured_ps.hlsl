cbuffer PerObjectCB : register(b0)
{
    float4x4 g_MVP;
    float4x4 g_World;
    float4x4 g_NormalMatrix;
    float4   g_ObjectParams; // z > 0.5 marks a transparent replay packet.
    uint     g_MaterialInstanceId;
    uint3    g_ObjectCapturePadding;
};

cbuffer MaterialCB : register(b1)
{
    float4 g_BaseColor;
    float4 g_EmissiveMetallic;
    float4 g_SurfaceParams;
    float4 g_SpecularParams;
    uint4  g_ModelFlags;
    float4 g_AlphaParams;
    uint4  g_TextureCoordinates0;
    uint4  g_TextureCoordinates1;
    float4 g_TextureParams;
    float4 g_TextureTransformRows[5];
    float4 g_TextureTransformOffsets[5];
};

Texture2D<float4> g_BaseColorMap : register(t0);
SamplerState g_BaseColorSampler : register(s0);
// Core clears both UAVs for each capture and fails capture if status byte offset 0 becomes nonzero.
// The replay pass must disable depth testing so depth-rejected fragments still reach this shader.
RWTexture2D<uint> g_OverdrawCounter : register(u0);
RWByteAddressBuffer g_OverdrawOverflow : register(u1);

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 UV0 : TEXCOORD2;
    float2 UV1 : TEXCOORD4;
    float2 UV2 : TEXCOORD6;
    float2 UV3 : TEXCOORD7;
    float2 UV4 : TEXCOORD8;
    float2 UV5 : TEXCOORD9;
    float2 UV6 : TEXCOORD10;
    float2 UV7 : TEXCOORD11;
    float4 Color : TEXCOORD5;
};

float2 GetTextureCoordinate(uint index, PS_INPUT input)
{
    switch (index)
    {
    case 0u: return input.UV0;
    case 1u: return input.UV1;
    case 2u: return input.UV2;
    case 3u: return input.UV3;
    case 4u: return input.UV4;
    case 5u: return input.UV5;
    case 6u: return input.UV6;
    default: return input.UV7;
    }
}

bool IncrementOverdraw(uint2 pixel)
{
    uint observed;
    InterlockedCompareExchange(g_OverdrawCounter[pixel], 0u, 0u, observed);

    [loop]
    for (;;)
    {
        if (observed == 0xffffffffu)
        {
            return true;
        }

        uint original;
        InterlockedCompareExchange(g_OverdrawCounter[pixel], observed, observed + 1u, original);
        if (original == observed)
        {
            return false;
        }
        observed = original;
    }
}

void main_ps(PS_INPUT input)
{
    const uint baseCoordinateIndex = g_TextureCoordinates0.x;
    const float2 baseCoordinate = GetTextureCoordinate(baseCoordinateIndex, input);
    const float2 baseUv = float2(dot(baseCoordinate, g_TextureTransformRows[0].xy),
                                 dot(baseCoordinate, g_TextureTransformRows[0].zw)) +
                          g_TextureTransformOffsets[0].xy;
    const float sourceOpacity = g_BaseColor.a * input.Color.a * g_BaseColorMap.Sample(g_BaseColorSampler, baseUv).a;
    const bool isMasked = (g_ModelFlags.w & 0xffu) == 1u;
    if (isMasked && sourceOpacity < g_AlphaParams.x)
    {
        discard;
    }
    if (g_ObjectParams.z > 0.5f && sourceOpacity <= 0.0f)
    {
        discard;
    }

    if (IncrementOverdraw(uint2(input.Position.xy)))
    {
        uint previousOverflow;
        g_OverdrawOverflow.InterlockedOr(0u, 1u, previousOverflow);
    }
}
