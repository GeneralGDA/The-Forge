#include "01_Emitter.h"

#include <algorithm>
#include <limits>

#include "../../../../Common_3/OS/Interfaces/ILogManager.h"

#undef max

namespace
{

const auto LIFE_LENGTH_SECONDS = 4.0f;
const auto PARTICLES_PER_SECOND = 10.0f;
const auto PARTICLE_EMIT_PERIOD = 1.0f / PARTICLES_PER_SECOND;

const vec3 EMITTER_POSITION{10.0f, 0.0f, 0.0f};
const auto EMITTER_EMIT_CUBE_HALF_SIZE = 0.5f;

const vec3 PARTICLES_VELOCITY{0.0f, 4.0f, 0.0f};

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
}

void Emitter::emitParticle(const int index, const float startTime)
{
	ASSERT(0 <= index);

	auto& particle = particles[index];
	particle.aliveTime = startTime;
	particle.styleNumber = rand() / static_cast<float>(stylesCount);
	particle.position = EMITTER_POSITION + randomShift(EMITTER_EMIT_CUBE_HALF_SIZE);
}

void Emitter::sortParticles(const mat4& camera)
{
	for (auto& particle : particles)
	{
		particle.cameraSpaceZ = dot(camera[2], vec4{particle.position, 1.0f});
	}

	std::sort(particles.begin(), particles.end()
	, 
		[](const auto& left, const auto& right)
		{
			return left.cameraSpaceZ < right.cameraSpaceZ;
		}
	);
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

void Emitter::update(const float timeDeltaSeconds, const mat4& camera)
{
	ASSERT(0 <= timeDeltaSeconds);

	dead.clear();

	const auto effectiveTimeDelta = timeDeltaSeconds + timeRest;
	auto particlesToEmit = static_cast<int>(effectiveTimeDelta * PARTICLES_PER_SECOND);
	timeRest = effectiveTimeDelta - (particlesToEmit * PARTICLE_EMIT_PERIOD);

	for (auto i = 0u; i < particles.size(); ++i)
	{
		auto& particle = particles[i];

		particle.aliveTime += timeDeltaSeconds;

		if (LIFE_LENGTH_SECONDS < particle.aliveTime)
		{
			if (0 < particlesToEmit)
			{
				emitParticle(i, particlesToEmit * PARTICLE_EMIT_PERIOD);
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
			emitParticle(checkedCast(emitIndex), particlesToEmit * PARTICLE_EMIT_PERIOD);
			++emitIndex;
			--particlesToEmit;
		}
	}
	else
	{
		removeDeadParticles();
	}

	sortParticles(camera);

	outBehaviors.clear();
	outPositions.clear();

	for (auto& particle : particles)
	{
		outPositions.push_back(particle.position.getX());
		outPositions.push_back(particle.position.getY());
		outPositions.push_back(particle.position.getZ());
		
		outBehaviors.push_back(particle.aliveTime);
		outBehaviors.push_back(particle.styleNumber);
	}
}

int Emitter::getAliveParticlesCount() const
{
	return checkedCast(particles.size());
}

const float* Emitter::getPositions() const
{
	return outPositions.data();
}

const float* Emitter::getBehaviors() const
{
	return outBehaviors.data();
}
