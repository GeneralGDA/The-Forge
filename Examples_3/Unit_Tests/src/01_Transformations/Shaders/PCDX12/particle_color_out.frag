#define MAX_PLANETS 20

cbuffer uniformBlock : register(b0)
{
    float4x4 mvp;
	float4x4 camera;
	float4 zProjection; // x <- scale, y <- bias
    float4x4 toWorld[MAX_PLANETS];
    float4 color[MAX_PLANETS];

    // Point Light Information
    float3 lightPosition;
    float3 lightColor;
};

SamplerState particleImageSampler : register(s1);
Texture2D image : register(t1);

struct VSOutput 
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
	float alphaScale : ALPHA_MULTIPLIER;
	float3 color : COLOR;
};

float4 main(VSOutput input) : SV_TARGET
{
	float4 color = image.Sample(particleImageSampler, input.texCoord);
	color.rgb *= input.color;
	color.a *= input.alphaScale;

	return color;
}
