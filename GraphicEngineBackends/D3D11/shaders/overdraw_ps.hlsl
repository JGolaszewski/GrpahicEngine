cbuffer PerObjectCB : register(b0)
{
    float4x4 g_MVP;
    float4x4 g_World;
    float4x4 g_NormalMatrix;
    float4   g_ObjectParams;
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

RWTexture2D<uint> g_OverdrawCounter : register(u0);
RWStructuredBuffer<uint> g_OverdrawOverflow : register(u1);

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : TEXCOORD5;
};

void IncrementOverdraw(uint2 pixel)
{
    uint observed;
    InterlockedCompareExchange(g_OverdrawCounter[pixel], 0u, 0u, observed);

    [loop]
    for (;;)
    {
        if (observed == 0xffffffffu)
        {
            uint previousOverflow;
            InterlockedOr(g_OverdrawOverflow[0], 1u, previousOverflow);
            return;
        }

        uint original;
        InterlockedCompareExchange(g_OverdrawCounter[pixel], observed, observed + 1u, original);
        if (original == observed)
        {
            return;
        }
        observed = original;
    }
}

void main_ps(PS_INPUT input)
{
    const float sourceOpacity = g_BaseColor.a * input.Color.a;
    const bool isMasked = (g_ModelFlags.w & 0xffu) == 1u;
    if (isMasked && sourceOpacity < g_AlphaParams.x)
    {
        discard;
    }
    if (g_ObjectParams.z > 0.5f && sourceOpacity <= 0.0f)
    {
        discard;
    }

    IncrementOverdraw(uint2(input.Position.xy));
}
