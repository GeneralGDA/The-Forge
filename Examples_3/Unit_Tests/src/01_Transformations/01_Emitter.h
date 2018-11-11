#pragma once

#include <vector>

#include "../../../../Common_3/OS/Math/MathTypes.h"

class Emitter final
{
private:

	struct Particle final
	{
		vec3 position;
		
		float cameraSpaceZ;
		float aliveTime;
		float styleNumber;
	};

	std::vector<Particle> particles;

	std::vector<float> outPositions;
	std::vector<float> outBehaviors;

	std::vector<int> dead;

	const int maxParticlesCount;
	const int stylesCount;

	float timeRest;

	void emitParticle(int index, float startTime);
	void removeDeadParticles();
	void sortParticles(const mat4& camera);

public:

	static constexpr auto LIFE_LENGTH_SECONDS = 8.0f;

	Emitter(int maxParticlesCount, int stylesCount);

	void update(float timeDeltaSeconds, const mat4& camera);

	int getAliveParticlesCount() const;

	const float* getPositions() const;
	const float* getBehaviors() const;

};