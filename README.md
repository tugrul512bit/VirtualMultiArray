# VirtualMultiArray
Multi graphics card based C++ virtual array implementation that uses OpenCL just for the data transfers on pcie bridge.

Wiki: https://github.com/tugrul512bit/VirtualMultiArray/wiki/How-it-works

```
GraphicsCardSupplyDepot depot;

const size_t n = 1024*10000;
const size_t pageSize=1024;
const int maxActivePagesPerGpu = 100;

// uses VRAMs for 10M particles
// uses RAM for paging (active pages)
VirtualMultiArray<Particle> particleArray(n,depot.requestGpus(),pageSize,maxActivePagesPerGpu);

VirtualMultiArray<int> intArray(1000000,depot.requestGpus(),100,10);

particleArray.set(5,Particle(31415));
intArray.set(0,3); // or intArray[0]=3;
intArray.set(1,1); 
intArray.set(2,4); 
intArray.set(3,1);
intArray.set(4,5); // or intArray[4]=5;

std::cout<<particleArray.get(5).getId()<<std::endl;
std::cout<<intArray.get(5)<<std::endl;
Particle p = particleArray[5];
std::cout<<p.getId()<<std::endl;
```
