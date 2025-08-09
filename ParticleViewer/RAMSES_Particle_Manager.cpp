#include "RAMSES_Particle_Manager.h"

// RAMSES
#include "ramses/RAMSES_info.hh"
#include "ramses/RAMSES_particle_data.hh"

#include <omp.h>

RAMSES_Particle_Manager::RAMSES_Particle_Manager(std::string filename)
{
	std::cout << "ParticleManager reading RAMSES dataset." << std::endl;
	// Load the RAMSES particle data into vector<particle> arrays
	RAMSES::snapshot rsnap(filename, RAMSES::version3);
	std::cout << "aexp = " << rsnap.m_header.aexp << std::endl;
	// Reserve memory in particle vector
	int npart = (int)std::pow((double)rsnap.m_header.levelmax, 3.0f);
	bool dmonly = false;
	bool ompEnabled = false;

	std::vector<Particle> vec;
	std::vector<std::vector<Particle>> buffers;

	if (ompEnabled)
	{
#pragma omp parallel
		{
			auto nthreads = omp_get_num_threads();
			auto id = omp_get_thread_num();
			// Correctly set number of buffers
#pragma omp single
			{
				std::cout << "OMP enabled with " << nthreads << " threads." << std::endl;
				buffers.resize(nthreads);
			}
			// Each thread pushes to own buffer
	//#pragma omp for schedule(static)
#pragma omp parallel for
			for (int icpu = 1; icpu <= rsnap.m_header.ncpu; icpu++)
			{
				RAMSES::PART::data local_data(rsnap, icpu);
				std::vector<float> x, y, z, age;
				local_data.get_var<double>("position_x", std::back_inserter(x));

				y.reserve(x.size());
				z.reserve(x.size());
				age.reserve(x.size());

				local_data.get_var<double>("position_y", std::back_inserter(y));
				local_data.get_var<double>("position_z", std::back_inserter(z));

				try
				{
					local_data.get_var<double>("age", std::back_inserter(age));
				}
				catch (...) {
					dmonly = true;
				}
				delete &local_data;

				for (size_t i = 0; i < x.size(); i++)
				{
					if (dmonly)
					{
						glm::vec3 pos(x[i], y[i], z[i]);
						Particle newParticle(pos);
						buffers[id].push_back(newParticle);
					}
					else
					{
						if (age[i] == 0)
						{
							glm::vec3 pos(x[i], y[i], z[i]);
							Particle newParticle(pos);
							buffers[id].push_back(newParticle);
						}
					}
				}
			}

			// Combine the buffers
#pragma omp single
			for (auto & buffer : buffers)
			{
				std::move(buffer.begin(), buffer.end(), std::back_inserter(vec));
			}
		}
	}
	else {
		for (int icpu = 1; icpu <= rsnap.m_header.ncpu; icpu++)
		{
			std::cout << "Reading icpu " << icpu << "/" << rsnap.m_header.ncpu << std::endl;
			RAMSES::PART::data data(rsnap, icpu);
			std::vector<float> x, y, z, age;
			data.get_var<double>("position_x", std::back_inserter(x));

			y.reserve(x.size());
			z.reserve(x.size());
			age.reserve(x.size());

			data.get_var<double>("position_y", std::back_inserter(y));
			data.get_var<double>("position_z", std::back_inserter(z));

			try
			{
				data.get_var<double>("age", std::back_inserter(age));
			}
			catch (...) {
				dmonly = true;
			}

			for (size_t i = 0; i < x.size(); i++)
			{
				if (dmonly)
				{
					glm::vec3 pos(x[i], y[i], z[i]);
					Particle newParticle(pos);
					vec.push_back(newParticle);
				}
				else
				{
					if (age[i] == 0)
					{
						glm::vec3 pos(x[i], y[i], z[i]);
						Particle newParticle(pos);
						vec.push_back(newParticle);
					}
				}
			}
		}
	}

	std::random_shuffle(vec.begin(), vec.end());
	this->mParticleArray = vec;
	this->npart = this->mParticleArray.size();
	std::cout << "Successfully loaded " << this->mParticleArray.size() << " particles."  << std::endl;
}

GLfloat * RAMSES_Particle_Manager::particlesArray()
{
	GLfloat *pArray = new float[this->npartDraw * 3];

	//int di = this->mParticleArray.size() / npartDraw;
	for (int i = 0; i < this->npartDraw; i++)
	{
		int idx = i * 3;
		pArray[idx] = this->mParticleArray[i].position.x;
		pArray[idx + 1] = this->mParticleArray[i].position.y;
		pArray[idx + 2] = this->mParticleArray[i].position.z;
	}

	return pArray;
}


RAMSES_Particle_Manager::~RAMSES_Particle_Manager()
{
}
