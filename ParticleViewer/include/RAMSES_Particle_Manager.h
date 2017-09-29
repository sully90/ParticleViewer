#ifndef RAMSES_PARTICLE_MANAGER_H
#define RAMSES_PARTICLE_MANAGER_H

// GLEW
#define GLEW_STATIC
#include <GL/glew.h>

// GLFW
#include <GLFW/glfw3.h>

// GLM
#include <glm\glm.hpp>

#include <iostream>
#include <vector>

#include "Particle.h"

class RAMSES_Particle_Manager
{
public:
	RAMSES_Particle_Manager(std::string filename);

	int npart;
	const int npartDraw = std::pow(32, 3);
	GLfloat *particlesArray();
	std::vector<Particle> mParticleArray;

	~RAMSES_Particle_Manager();
private:
	
};

#endif