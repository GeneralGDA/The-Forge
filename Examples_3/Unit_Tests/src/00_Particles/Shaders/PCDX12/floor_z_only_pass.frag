struct VertexShaderOutput 
{
	float4 projectedPosition : SV_POSITION;
	float4 worldSpacePosition: POSITION;
};

float4 main(VertexShaderOutput input) : SV_TARGET
{
	return float4(0, 0, 0, 0);
}
