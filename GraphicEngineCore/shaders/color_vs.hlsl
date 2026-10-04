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
    float2 UV     : TEXCOORD;
};

// Output structure (to pixel shader)
struct VS_OUTPUT
{
    float4 pos      : SV_POSITION;
    float3 normalWS : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float2 UV       : TEXCOORD2;
};

// MAIN VERTEX SHADER FUNCTION
VS_OUTPUT main_vs(VS_INPUT input)
{
    VS_OUTPUT output;
    float4 worldPos = mul(float4(input.pos, 1.0f), g_World);

    output.pos      = mul(float4(input.pos, 1.0f), g_MVP);
    output.normalWS = normalize(mul(float4(input.normal, 0.0f), g_NormalMatrix).xyz);
    output.worldPos = worldPos.xyz;

    output.UV       = input.UV; 

    return output;
}
