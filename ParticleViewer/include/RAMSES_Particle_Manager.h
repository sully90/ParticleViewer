#ifndef RAMSES_PARTICLE_MANAGER_H
#define RAMSES_PARTICLE_MANAGER_H

// GLEW
#include <GL/glew.h>

// GLFW
#include <GLFW/glfw3.h>

// GLM
#include <glm/glm.hpp>

#include <iostream>
#include <vector>

#include "Particle.h"

class RAMSES_Particle_Manager
{
public:
	RAMSES_Particle_Manager(std::string filename);

    int npart;
    // Number of particles to draw (capped in constructor)
    int npartDraw;
    // Limit how many particles to draw (-1 = all)
    void setMaxParticles(long long maxParticles);
	GLfloat *particlesArray();
	std::vector<Particle> mParticleArray;

	~RAMSES_Particle_Manager();
private:
    long long m_maxParticles{-1};
	
};

#endif