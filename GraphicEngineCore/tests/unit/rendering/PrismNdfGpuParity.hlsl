#include "brdf_common.hlsli"

struct PrismNdfCase
{
    float4 coefficients[12];
    float2 diskCoord;
    float residual;
    float normalization;
};

StructuredBuffer<PrismNdfCase> g_Cases : register(t0);
RWStructuredBuffer<float> g_Results : register(u0);

[numthreads(64, 1, 1)]
void main_cs(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint caseCount;
    uint caseStride;
    g_Cases.GetDimensions(caseCount, caseStride);
    if (dispatchThreadId.x >= caseCount)
    {
        return;
    }

    const PrismNdfCase testCase = g_Cases[dispatchThreadId.x];
    g_Results[dispatchThreadId.x] = PrismZernikeNdf(
        testCase.diskCoord, testCase.coefficients, testCase.residual, testCase.normalization);
}
