// Bufor od Renderera - zmienia siê dla ka¿dego obiektu!
cbuffer PerObjectCB : register(b0)
{
	float4x4 g_MVP;
  float4x4 g_World;
  float4 g_ObjectParams;
};

// Struktura wejœciowa (z Mesha)
struct VS_INPUT
{
	float3 pos : POSITION;
  float3 normal : NORMAL;
};

// Struktura wyjœciowa (do Pixel Shadera)
struct VS_OUTPUT
{
	float4 pos : SV_POSITION;
 float3 normalWS : TEXCOORD0;
	float3 worldPos : TEXCOORD1;
};

// G£ÓWNA FUNKCJA VERTEX SHADERA
VS_OUTPUT main_vs(VS_INPUT input)
{
	VS_OUTPUT output;
	float4 worldPos = mul(float4(input.pos, 1.0f), g_World);
	
	output.pos = mul(float4(input.pos, 1.0f), g_MVP);
	output.normalWS = normalize(mul(float4(input.normal, 0.0f), g_World).xyz);
	output.worldPos = worldPos.xyz;
	
	return output;
}