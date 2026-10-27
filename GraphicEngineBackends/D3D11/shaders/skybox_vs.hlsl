struct VS_OUTPUT
{
    float4 position : SV_POSITION;
};

VS_OUTPUT main_vs(uint vertexId : SV_VertexID)
{
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    VS_OUTPUT output;
    output.position = float4(corner * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}
