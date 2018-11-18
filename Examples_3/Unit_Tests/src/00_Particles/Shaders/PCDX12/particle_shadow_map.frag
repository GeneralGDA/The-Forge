SamplerState particleImageSampler : register(s1);
Texture2D image : register(t1);

struct VertexShaderOutput 
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
	float alphaScale : ALPHA_MULTIPLIER;
	float3 color : COLOR;
	float cameraSpaceDepth : LINEAR_DEPTH;
};

struct PixelShaderOutput
{
	float4 color : SV_Target0;
	float4 cameraSpaseDepth : SV_Target1;
};

PixelShaderOutput main(VertexShaderOutput input)
{
	PixelShaderOutput result;
	result.color = image.Sample(particleImageSampler, input.texCoord);
	
	result.color.rgb *= input.color;
	result.color.a *= input.alphaScale;

	result.cameraSpaseDepth = input.cameraSpaceDepth.xxxx;

	return result;
}
