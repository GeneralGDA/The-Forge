#define MAX_PLANETS 20

cbuffer uniformBlock : register(b0)
{
    float4x4 mvp;
	float4x4 camera;
};

#define MAX_PARTICLES_COUNT 200
#define PARTICLES_STYLES_COUNT 3

cbuffer particlesInstances : register(b1)
{
	float4 positions[MAX_PARTICLES_COUNT];
	float4 timeAndStyle[MAX_PARTICLES_COUNT];

	float4 colorAndSizeScale[PARTICLES_STYLES_COUNT];

	float particleLifeTime;
};

struct VertexShaderOutput 
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
	float alphaScale : ALPHA_MULTIPLIER;
	float3 color : COLOR;
	float cameraSpaceDepth : LINEAR_DEPTH;
};

float sizeScaleFromAge(float age)
{
	return 5.0f + age * 20.0f;
}

float alphaScaleFromAge(float age)
{
	const float fadeInEndEnd = 0.1f;
	const float fadeInOutStart = 0.3f;

	const float baseAlpha = 0.1f;

	if (age < fadeInEndEnd)
	{
		return baseAlpha * (fadeInEndEnd - age) / fadeInEndEnd;
	}

	if (age > fadeInOutStart)
	{
		return baseAlpha * (1.0f - (age - fadeInOutStart) / (1.0f - fadeInOutStart));
	}
	
	return baseAlpha;
}

VertexShaderOutput main(float4 size : POSITION, uint InstanceID : SV_InstanceID)
{
	VertexShaderOutput result;

	float aliveTime = timeAndStyle[InstanceID].x;
	float styleIndex = timeAndStyle[InstanceID].y;

	float timeRelative = aliveTime / particleLifeTime;

	float sizeScale = sizeScaleFromAge(timeRelative) * colorAndSizeScale[styleIndex].w;
	float alphaScale = alphaScaleFromAge(timeRelative);

	float3 xAxis = camera[0].xyz;
	float3 yAxis = camera[1].xyz;
	float3 position = positions[InstanceID].xyz + (xAxis * (size.x * sizeScale)) + (yAxis * (size.y * sizeScale));

    result.position = mul(mvp, float4(position, 1.0f));
	result.cameraSpaceDepth = dot(camera[2], float4(position, 1.0f));
	result.alphaScale = alphaScale;
	result.color = colorAndSizeScale[styleIndex].rgb;

	if (size.x > 0 && size.y > 0)
	{
		result.texCoord = float2(1, 0);
	}
	else if (size.x > 0 && size.y < 0)
	{
		result.texCoord = float2(1, 1);
	}
	else if (size.x < 0 && size.y < 0)
	{
		result.texCoord = float2(0, 1);
	}
	else if (size.x < 0 && size.y > 0)
	{
		result.texCoord = float2(0, 0);
	}

	return result;
}
