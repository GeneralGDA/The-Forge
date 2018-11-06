#define MAX_PLANETS+ 20

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

#define MAX_PARTICLES_COUNT 300

cbuffer particlesInstances : register(b1)
{
	float3 positions[MAX_PARTICLES_COUNT];
	float2 timeAndStyle[MAX_PARTICLES_COUNT];
};

struct VSOutput 
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
	float cameraSpaceDepth : LINEAR_DEPTH;
};

VSOutput main(float4 size : POSITION, uint InstanceID : SV_InstanceID)
{
	VSOutput result;

	float3 center = float3(100, 0, 0);
	float3 xAxis = camera[0].xyz;
	float3 yAxis = camera[1].xyz;
	float3 position = positions[InstanceID] + xAxis * size.x + yAxis * size.y;
 
    result.position = mul(mvp, float4(position, 1.0f));
    result.cameraSpaceDepth = dot(camera[2], float4(position, 1.0f));

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
