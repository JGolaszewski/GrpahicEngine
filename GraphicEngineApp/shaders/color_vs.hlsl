// Bufor od Renderera - zmienia siê dla ka¿dego obiektu!
cbuffer PerObjectCB : register(b0)
{
	float4x4 g_MVP;
};

// Struktura wejœciowa (z Mesha)
struct VS_INPUT
{
	float3 pos : POSITION;
};

// Struktura wyjœciowa (do Pixel Shadera)
struct VS_OUTPUT
{
	float4 pos : SV_POSITION;
};

// G£ÓWNA FUNKCJA VERTEX SHADERA
VS_OUTPUT main_vs(VS_INPUT input)
{
	VS_OUTPUT output;
	
	// Magia 3D: Przemna¿amy lokaln¹ pozycjê szeœcianu przez macierz kamery i perspektywy
	// Funkcja mul() wykonuje mno¿enie macierzy przez wektor.
	output.pos = mul(float4(input.pos, 1.0f), g_MVP);
	
	return output;
}