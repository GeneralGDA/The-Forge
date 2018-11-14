struct VSOutput 
{
	float4 position : SV_POSITION;
	float cameraSpaceDepth : LINEAR_DEPTH;
};

float4 main(VSOutput input) : SV_TARGET
{
	return input.cameraSpaceDepth.xxxx;
}
