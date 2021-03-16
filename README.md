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
		size_t numElements = 2000;
		int pageSize = 10;
		int activePagesPerGpuInstance = 5;

		// not just "int" but any POD of hundreds of kilobytes size too
		// A "Particle" object with 40-50 bytes is much more efficient for pcie data transfers, than an int, unless page size (cache line) is big enough
		VirtualMultiArray<int> intArr(numElements,d.requestGpus(),pageSize,activePagesPerGpuInstance);

		// thread-safe (scales up to 8 threads per logical core): #pragma omp parallel for num_threads(64)
		for(size_t i=0;i<numElements;i++)
			intArr.set(i,i*2);
					
		for(size_t i=0;i<numElements;i++)
		{
			int var = intArr.get(i);
			std::cout<<var<<std::endl;
			
			// just a single-threaded-access optimization
			// to hide pcie latency
			if((i<numElements-pageSize) && (i%pageSize==0) )
			{
				intArr.prefetch(i+pageSize); // asynchronously load next page into LRU
			}
		}
	}
	catch (std::exception& ex)
	{
		std::cout << ex.what();
	}
	return 0;
}
```
