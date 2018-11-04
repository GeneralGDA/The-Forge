#define MAX_PLANETS 20

cbuffer uniformBlock : register(b0)
{
    float4x4 mvp;
	float4x4 camera;
    float4x4 toWorld[MAX_PLANETS];
    float4 color[MAX_PLANETS];

    // Point Light Information
    float3 lightPosition;
    float3 lightColor;
};

struct VSOutput {
	float4 Position : SV_POSITION;
};

VSOutput main(float4 Position : POSITION)
{
	VSOutput result;
 
    result.Position = mul(mvp,Position);
    
	return result;
}
