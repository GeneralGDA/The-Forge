cbuffer ProjectionUniforms : register(b0)
{
    float4x4 mvp;
	float4x4 camera;

	float4 zProjection; // x <- scale, y <- bias
};

struct VertexShaderOutput 
{
	float4 projectedPosition : SV_POSITION;
	float4 worldSpacePosition: POSITION;
};

VertexShaderOutput main(float4 worldSpacePosition : POSITION)
{
	VertexShaderOutput result;
 
    result.projectedPosition = mul(mvp, worldSpacePosition);
    result.worldSpacePosition = worldSpacePosition;

	return result;
}
