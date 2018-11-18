cbuffer uniformBlock : register(b0)
{
    float4x4 mvp;
	float4x4 camera;
	float4 zProjection; // x <- scale, y <- bias
};

SamplerState particleImageSampler : register(s1);
Texture2D image : register(t1);
Texture2D depthBuffer : register(t2);

struct VSOutput 
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
	float cameraSpaceDepth : LINEAR_DEPTH;
	float alphaScale : ALPHA_MULTIPLIER;
	float3 color : COLOR;
};

float readDepth(float4 screenPosition)
{
	float nonLinearDepth = depthBuffer.Load(int3(screenPosition.xy, 0)).r;
	
	return zProjection.y / (nonLinearDepth - zProjection.x);
}

float softenParticle(float currentDepth, float bufferDepth)
{
	float threshold = 10;
	float depthDelta = bufferDepth - currentDepth;

	if (threshold < depthDelta)
	{
		return 1;
	}

	return smoothstep(0, threshold, depthDelta);
}

float4 main(VSOutput input) : SV_TARGET
{
	float bufferDepth = readDepth(input.position);
	float alphaMultiplier = softenParticle(input.cameraSpaceDepth, bufferDepth);
	
	float4 color = image.Sample(particleImageSampler, input.texCoord);
	color.rgb *= input.color;
	color.a *= alphaMultiplier * input.alphaScale;

	return color;
}
