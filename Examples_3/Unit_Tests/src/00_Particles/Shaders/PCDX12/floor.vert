cbuffer uniformBlock : register(b0)
{
    float4x4 mvp;
	float4x4 camera;
	float4 zProjection; // x <- scale, y <- bias
};

struct VertexShaderOutput 
{
	float4 Position : SV_POSITION;
	float4 worldSpacePosition: POSITION;
};

VertexShaderOutput main(float4 Position : POSITION)
{
	VertexShaderOutput result;
 
    result.Position = mul(mvp, Position);
    result.worldSpacePosition = Position;

	return result;
}
