cbuffer uniformBlock : register(b0)
{
    float4x4 mvp;
	float4x4 camera;
	float4 zProjection; // x <- scale, y <- bias

    float3 lightPosition;
    float3 lightColor;
};

struct VSOutput 
{
	float4 Position : SV_POSITION;
	float4 worldSpacePosition: POSITION;
};

VSOutput main(float4 Position : POSITION)
{
	VSOutput result;
 
    result.Position = mul(mvp,Position);
    result.worldSpacePosition = Position;

	return result;
}
