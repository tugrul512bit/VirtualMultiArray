# VirtualMultiArray
Multi graphics card based virtual array for C++ objects

const size_t n = 1024*10000;
const size_t pageSize=1024;
const int maxActivePagesPerGpu = 100;

// uses VRAMs for 10M particles
// uses RAM for paging (active pages)
VirtualMultiArray<Particle> test(n,device,pageSize,maxActivePagesPerGpu);

test.set(5,Particle(31415));

std::cout<<test.get(5).getId()<<std::endl;
