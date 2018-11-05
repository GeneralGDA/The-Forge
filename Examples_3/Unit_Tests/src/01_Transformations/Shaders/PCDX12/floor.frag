#define MAX_PLANETS 20

cbuffer uniformBlock : register(b0)
{
    float4x4 mvp;
	float4x4 camera;
	float4 zProjection; // x <- scale, y <- bias
    float4x4 toWorld[MAX_PLANETS];
    float4 color[MAX_PLANETS];

    // Point Light Information
    float3 lightPosition;
    float3 lightColor;
};

struct VSOutput 
{
	float4 Position : SV_POSITION;
};

float4 main(VSOutput input) : SV_TARGET
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
