// Virtual Object Array that uses all graphics cards in system as storage
// to achieve seamless concurrent access to all elements
// example: 4.5GB "Particle" array distributed to 3 graphics cards, each serving 1.5GB of its VRAM
//                 RAM only used for paging system, it can be altered to use more/less of it (number of active pages parameter)
// v1.0:
//		only get() set() methods, no defragmentation, no prefetching
//		array size needs to be integer multiple of page size
//		object size can be anything (4kB object size makes random access performance much better than an average SSD)
//									(44byte object needs sequential access to be fast)


// if windows with 64bit cpu + msvc++
#ifndef _AMD64_
#define _AMD64_
#endif

#include "GraphicsCardSupplyDepot.h"
#include "VirtualMultiArray.h"
#include "CpuBenchmarker.h"

class Particle
{
public:
	Particle() :x(0), y(0), z(0), vx(0), vy(0), vz(0), vx_old(0), vy_old(0), vz_old(0), m(0), id(0) {}
	Particle(int idP) :x(0), y(0), z(0), vx(0), vy(0), vz(0), vx_old(0), vy_old(0), vz_old(0), m(0), id(idP) { }
	int getId() { return id; }
private:
	float x, y, z;
	float vx, vy, vz;
	float vx_old, vy_old, vz_old;
	float m;
	int id;
};



int main(int argC, char** argV)
{
	GraphicsCardSupplyDepot depot;

	// n needs to be integer multiple of pageSize !!!!
	const size_t n = 1024 * 30000;
	const size_t pageSize = 1024;
	const int maxActivePagesPerGpu = 16;

	VirtualMultiArray<Particle> test;
	
	{
		CpuBenchmarker bench(0, "init");
		test = VirtualMultiArray<Particle>(n, depot.requestGpus(), pageSize, maxActivePagesPerGpu, {15,15,15,15,15,15,15,15,15,15,15});
	}

	for(int j=0;j<5;j++)
	{
		CpuBenchmarker bench(10000*sizeof(Particle), "single threaded set, uncached", 10000);
		for (int i = 0; i < 10000; i++)
		{
			test.set(i * (pageSize + 1), Particle(i * (pageSize + 1)));
		}
	}

	for (int j = 0; j < 5; j++)
	{
		CpuBenchmarker bench(10000 * sizeof(Particle), "single threaded ---get---, uncached", 10000);
		for (int i = 0; i < 10000; i++)
		{
			auto var = test.get(i * (pageSize + 1));
			if (var.getId() != i * (pageSize + 1))
			{
				std::cout << "Error!" << std::endl;
			}
		}
	}

	for (int j = 0; j < 5; j++)
	{
		CpuBenchmarker bench(10000*sizeof(Particle), "single threaded set, cached", 10000);
		for (int i = 0; i < 10000; i++)
			test.set(i, Particle(i));
	}

	for (int j = 0; j < 5; j++)
	{
		CpuBenchmarker bench(10000*sizeof(Particle), "single threaded ---get---, cached", 10000);
		for (int i = 0; i < 10000; i++)
		{
			auto var = test.get(i);
			if (var.getId() != i)
			{
				std::cout << "Error!" << std::endl;
			}
		}
	}


	{
		CpuBenchmarker bench(n*sizeof(Particle), "multithreaded sequential set",n);
		#pragma omp parallel for schedule(guided)
		for (int i = 0; i < n; i++)
		{
			test.set(i, Particle(i));
		}
	}

	{
		CpuBenchmarker bench(n * sizeof(Particle), "multithreaded sequential get", n);
		#pragma omp parallel for schedule(guided)
		for (int i = 0; i < n; i++)
		{
			if (test.get(i).getId() != i)
			{
				std::cout << "!!! error at " << i << std::endl;
			}
		}
	}
	return 0;
}