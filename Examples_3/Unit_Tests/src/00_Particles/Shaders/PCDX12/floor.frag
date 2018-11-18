cbuffer uniformBlock : register(b0)
{
    float4x4 mvp;
	float4x4 camera;
	float4 zProjection; // x <- scale, y <- bias
    
    float3 lightPosition;
    float3 lightColor;
};

cbuffer shadowReceiverUniforms : register(b1)
{
	float4x4 shadowMapMvp;
	float4x4 shadowMapCamera;
};

SamplerState shadowMapSampler : register(s1);
Texture2D shadowMapDepth : register(t1);
Texture2D shadowMapColor : register(t2);

struct VSOutput 
{
	float4 Position : SV_POSITION;
	float4 worldSpacePosition: POSITION;
};

float4 main(VSOutput input) : SV_TARGET
{
	float4 projected = mul(shadowMapMvp, input.worldSpacePosition);
	
	float2 shadowSampleLocation;
    shadowSampleLocation.x = (+(projected.x / projected.w) / 2.0f) + 0.5f;
    shadowSampleLocation.y = (-(projected.y / projected.w) / 2.0f) + 0.5f;
	
	float lightSpaceDepth = shadowMapDepth.Sample(shadowMapSampler, shadowSampleLocation).r;

	float4 floorColor = float4(1,1,1,1);

	float thisPixelLightSpaceDepth = dot(shadowMapCamera[2], input.worldSpacePosition);

	if (lightSpaceDepth < thisPixelLightSpaceDepth)
	{
		float4 colorFilter = shadowMapColor.Sample(shadowMapSampler, shadowSampleLocation).rgba;
		return float4(floorColor.rgb * colorFilter.rgb, 1);
	}

    return floorColor;
}
