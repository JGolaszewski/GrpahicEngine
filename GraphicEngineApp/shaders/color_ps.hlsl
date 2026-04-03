// Bufor od Materia³u - przeniesiony na b1!
cbuffer MaterialCB : register(b1)
{
	float4 g_BaseColor;
};

// Struktura wejœciowa (taka sama jak wyjœciowa z Vertex Shadera)
struct VS_OUTPUT
{
	float4 pos : SV_POSITION;
};

// G£ÓWNA FUNKCJA PIXEL SHADERA
float4 main_ps(VS_OUTPUT input) : SV_TARGET
{
	// Po prostu zwracamy kolor zdefiniowany w C++
	return g_BaseColor;
}