# VirtualMultiArray
Multi graphics card based C++ virtual array implementation that uses OpenCL just for the data transfers on pcie bridge.

Wiki: https://github.com/tugrul512bit/VirtualMultiArray/wiki/How-it-works

Simplest usage:
```
#include "GraphicsCardSupplyDepot.h"
#include "VirtualMultiArray.h"


// testing
#include <iostream>


int main(int argC, char ** argV)
{
	GraphicsCardSupplyDepot d;
	VirtualMultiArray<int> intArr(1000,d.requestGpus(),10,5);
	intArr[3]=5; // or intArr.set(3,5);
	int var = intArr[3]; // or intArr.get(3);
	std::cout<<var<<std::endl;
	return 0;
}
```

but its only optimized for medium to large sized objects due to pci-e bridge latency, for example a Particle class with x,y,z,vx,vy,vz,... fields is more efficient in access bandwidth than int.

```
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
std::cout<<p.getId()<<std::endl; // or std::cout<<particleArray.get(5)<<std::endl;
```
