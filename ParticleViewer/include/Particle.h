#ifndef PARTICLE_H
#define PARTICLE_H

#include <glm/glm.hpp>

class Particle
{
public:
	Particle(glm::vec3 position);

	glm::vec3 position;

	virtual ~Particle();
};

#endif