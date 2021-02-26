# VirtualMultiArray
Multi graphics card based C++ virtual array implementation that uses VRAM with OpenCL to save gigabytes of RAM.

Wiki: https://github.com/tugrul512bit/VirtualMultiArray/wiki/How-it-works

This tool enables:

- offloading some data to video ram to save disk from over-use 
- - such as for caching big files like FASTA nucleotide sequences : https://github.com/tugrul512bit/FastaGeneIndexer
- preserving responsiveness of system when more space than the available RAM is needed
- - with better latency than HDD/SSD
- - with higher throughput than NVME (requires multiple graphics cards)
- finding an element in array by using GPU compute power

Simplest usage:
```cpp
#include "GraphicsCardSupplyDepot.h"
#include "VirtualMultiArray.h"


// testing
#include <iostream>


int main(int argC, char ** argV)
{
	try
	{
		GraphicsCardSupplyDepot d;
		size_t numElements = 1000;
		int pageSize = 10;
		int activePagesPerGpuInstance = 5;

		VirtualMultiArray<int> intArr(numElements,d.requestGpus(),pageSize,activePagesPerGpuInstance);

		intArr[3]=5; // or intArr.set(3,5);
		int var = intArr[3]; // or intArr.get(3);
		std::cout<<var<<std::endl;
	}
	catch (std::exception& ex)
	{
		std::cout << ex.what();
	}
	return 0;
}
```

but its only optimized for medium to large sized objects due to pci-e bridge latency, for example a Particle class with x,y,z,vx,vy,vz,... fields is more efficient in access bandwidth than int.

```cpp
GraphicsCardSupplyDepot depot;

const size_t n = 1024*10000;
const size_t pageSize=1024;
const int maxActivePagesPerGpu = 100;

// uses VRAMs for 10M particles
// uses RAM for paging (active pages)
VirtualMultiArray<Particle> particleArray(n,depot.requestGpus(),pageSize,maxActivePagesPerGpu);

particleArray.set(5,Particle(31415));

std::cout<<particleArray.get(5).getId()<<std::endl;

Particle p = particleArray[5];
std::cout<<p.getId()<<std::endl

// returns indices of particles with id member value = p.id
// gpu-accelerated, does not use RAM bandwidth for searching
std::vector<size_t> indexArray = particleArray.find(p,p.id);

```
