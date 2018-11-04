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

SamplerState particleImageSampler : register(s8);
Texture2D image : register(t7);

struct VSOutput 
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
};

float4 main(VSOutput input) : SV_TARGET
{
	return image.Sample(particleImageSampler, input.texCoord);
}
