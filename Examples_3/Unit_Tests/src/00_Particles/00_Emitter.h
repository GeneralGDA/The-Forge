#pragma once

#include <vector>

#include "../../../../Common_3/OS/Math/MathTypes.h"

class Emitter final
{
private:

	struct Particle final
	{
		Particle()
			:
			position(0),
			cameraSpaceZ(0),
			aliveTime(0),
			styleNumber(0)
		{
		}

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

public:

	static constexpr auto LIFE_LENGTH_SECONDS = 8.0f;

	Emitter(int maxParticlesCount, int stylesCount);

	void update(float timeDeltaSeconds);

	int getAliveParticlesCount() const;

	void sort(const mat4& camera);

	const float* getPositions();
	const float* getBehaviors();

};