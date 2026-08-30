struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color    : COLOR0;
};

float4 main_ps(PS_INPUT input) : SV_TARGET
{
    return input.color;
}
