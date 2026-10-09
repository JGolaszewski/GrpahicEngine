// Per-object buffer supplied by the renderer - changes for every object!
cbuffer PerObjectCB : register(b0)
{
    float4x4 g_MVP;
    float4x4 g_World;
    float4x4 g_NormalMatrix;
    float4   g_ObjectParams;
};

// Input structure (from mesh)
struct VS_INPUT
{
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 UV0    : TEXCOORD0;
    float2 UV1    : TEXCOORD1;
    float2 UV2    : TEXCOORD2;
    float2 UV3    : TEXCOORD3;
    float2 UV4    : TEXCOORD4;
    float2 UV5    : TEXCOORD5;
    float2 UV6    : TEXCOORD6;
    float2 UV7    : TEXCOORD7;
    float4 tangent : TANGENT;
    float4 color : COLOR;
};

// Output structure (to pixel shader)
struct VS_OUTPUT
{
    float4 pos      : SV_POSITION;
    float3 normalWS : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float2 UV       : TEXCOORD2;
    float4 tangentWS : TEXCOORD3;
    float2 UV1 : TEXCOORD4;
    float4 color : TEXCOORD5;
    float2 UV2 : TEXCOORD6;
    float2 UV3 : TEXCOORD7;
    float2 UV4 : TEXCOORD8;
    float2 UV5 : TEXCOORD9;
    float2 UV6 : TEXCOORD10;
    float2 UV7 : TEXCOORD11;
};

// MAIN VERTEX SHADER FUNCTION
VS_OUTPUT main_vs(VS_INPUT input)
{
    VS_OUTPUT output;
    float4 worldPos = mul(float4(input.pos, 1.0f), g_World);

    output.pos      = mul(float4(input.pos, 1.0f), g_MVP);
    output.normalWS = normalize(mul(float4(input.normal, 0.0f), g_NormalMatrix).xyz);
    const float3 tangentWS = mul(float4(input.tangent.xyz, 0.0f), g_World).xyz;
    const float handedness = input.tangent.w * (determinant((float3x3)g_World) < 0.0f ? -1.0f : 1.0f);
    output.tangentWS = float4(normalize(tangentWS), handedness);
    output.worldPos = worldPos.xyz;

    output.UV       = input.UV0;
    output.UV1      = input.UV1;
    output.UV2      = input.UV2;
    output.UV3      = input.UV3;
    output.UV4      = input.UV4;
    output.UV5      = input.UV5;
    output.UV6      = input.UV6;
    output.UV7      = input.UV7;
    output.color    = input.color;

    return output;
}
