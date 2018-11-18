#include "00_Emitter.h"

#include <algorithm>
#include <limits>

#include "../../../../Common_3/OS/Interfaces/ILogManager.h"

#undef max
#undef min

namespace
{

const auto PARTICLES_PER_SECOND = 25.0f;
const auto PARTICLE_EMIT_PERIOD = 1.0f / PARTICLES_PER_SECOND;

const vec3 EMITTER_POSITION{0.0f, -1.0f, 30.0f};
const auto EMITTER_EMIT_CUBE_HALF_SIZE = 1.5f;

const vec3 PARTICLES_VELOCITY{0.0f, 4.0f, 0.0f};

const auto MAX_TIME_DELTA = 0.4f;

float random()
{
	return (rand() / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
}

vec3 randomShift(const float cubeHalfSize)
{
	return { random() * cubeHalfSize, random() * cubeHalfSize, random() * cubeHalfSize };
}

int checkedCast(const size_t value)
{
#ifdef _DEBUG

	static_assert(sizeof(value) >= sizeof(int), "such a strange platform: size_t is smaller than int");

	if (static_cast<size_t>(std::numeric_limits<int>::max()) < value)
	{
		ASSERT(false && "value is too large");
	}

#endif

	return static_cast<int>(value);
}

const auto FLOATS_PER_OUT_ELEMENT = 4;

} // namespace

Emitter::Emitter(const int _maxParticlesCount, const int _stylesCount)
	:
	maxParticlesCount(_maxParticlesCount), 
	stylesCount(_stylesCount),
	timeRest(0.0)
{
	ASSERT(0 < maxParticlesCount);
	ASSERT(0 < stylesCount);

	particles.reserve(_maxParticlesCount);

	outPositions.reserve(_maxParticlesCount * FLOATS_PER_OUT_ELEMENT);
	outBehaviors.reserve(_maxParticlesCount * FLOATS_PER_OUT_ELEMENT);
}

void Emitter::emitParticle(const int index, const float startTime)
{
	ASSERT(0 <= index && checkedCast(particles.size()) > index);

	auto& particle = particles[index];
	particle.aliveTime = startTime;
	particle.styleNumber = static_cast<float>(rand() % stylesCount);
	particle.position = EMITTER_POSITION + randomShift(EMITTER_EMIT_CUBE_HALF_SIZE) + PARTICLES_VELOCITY * startTime;
}

void Emitter::removeDeadParticles()
{
	ASSERT( std::is_sorted(dead.begin(), dead.end()) );

	for (int i = checkedCast(dead.size()) - 1; i >= 0; --i)
	{
		if (i != checkedCast(particles.size()) - 1)
		{
			particles[i] = particles.back();
		}
	}

	particles.resize(particles.size() - dead.size());
}

void Emitter::sort(const mat4& viewerFrame)
{
	const auto Z_AXIS = 2;

	for (auto& particle : particles)
	{
		particle.cameraSpaceZ = dot(viewerFrame[Z_AXIS], vec4{particle.position, 1.0f});
	}

	std::sort(particles.begin(), particles.end()
	, 
		[](const auto& left, const auto& right)
		{
			return left.cameraSpaceZ < right.cameraSpaceZ;
		}
	);
}

void Emitter::update(const float timeDeltaSeconds)
{
	ASSERT(0 <= timeDeltaSeconds);

	if (0 >= timeDeltaSeconds)
	{
		return;
	}

	dead.clear();

	const auto effectiveTimeDelta = std::min(timeDeltaSeconds, MAX_TIME_DELTA) + timeRest;
	auto particlesToEmit = static_cast<int>(effectiveTimeDelta * PARTICLES_PER_SECOND);

	timeRest = effectiveTimeDelta - (particlesToEmit * PARTICLE_EMIT_PERIOD);

	if (checkedCast(particles.size()) + particlesToEmit > maxParticlesCount)
	{
		particlesToEmit = maxParticlesCount - checkedCast(particles.size());
	}

	for (auto i = 0u; i < particles.size(); ++i)
	{
		auto& particle = particles[i];

		particle.aliveTime += timeDeltaSeconds;

		if (LIFE_LENGTH_SECONDS < particle.aliveTime)
		{
			if (0 < particlesToEmit)
			{
				emitParticle(i, (particlesToEmit - 1) * PARTICLE_EMIT_PERIOD);
				--particlesToEmit;
			}
			else
			{
				dead.push_back(i);
			}
		}
		else
		{
			particle.position += PARTICLES_VELOCITY * timeDeltaSeconds;
		}
	}

	if (0 < particlesToEmit)
	{
		ASSERT(dead.empty());

		auto emitIndex = particles.size();
		particles.resize(particles.size() + particlesToEmit);

		while (0 < particlesToEmit)
		{
			emitParticle(checkedCast(emitIndex), (particlesToEmit - 1) * PARTICLE_EMIT_PERIOD);
			++emitIndex;
			--particlesToEmit;
		}
	}
	else
	{
		removeDeadParticles();
	}
}

int Emitter::getAliveParticlesCount() const
{
	return checkedCast(particles.size());
}

const float* Emitter::getPositions()
{
	outPositions.clear();

	for (auto& particle : particles)
	{
		outPositions.push_back(particle.position.getX());
		outPositions.push_back(particle.position.getY());
		outPositions.push_back(particle.position.getZ());
		outPositions.push_back(0.0f);
	}

	return outPositions.data();
}

const float* Emitter::getBehaviors()
{
	outBehaviors.clear();
	
	for (auto& particle : particles)
	{
		outBehaviors.push_back(particle.aliveTime);
		outBehaviors.push_back(particle.styleNumber);
		outBehaviors.push_back(0.0f);
		outBehaviors.push_back(0.0f);
	}

	return outBehaviors.data();
}
