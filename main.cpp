// author: huseyin tugrul buyukisik
// Virtual Object Array that uses all graphics cards in system as storage
// to achieve seamless concurrent access to all elements
// example: 4.5GB "Particle" array distributed to 3 graphics cards, each serving 1.5GB of its VRAM
//                 RAM only used for paging system, it can be altered to use more/less of it (number of active pages parameter)
// v1.0:
//		only get() set() methods, no defragmentation, no prefetching
//		array size needs to be integer multiple of page size
//		object size can be anything (4kB object size makes random access performance much better than an average SSD)
//									(44byte object needs sequential access to be fast)

#include <iostream>
#include <vector>
#include <memory>

#include <mutex>
#include <functional>


#include "GraphicsCardSupplyDepot.h"
#include "VirtualMultiArray.h"

class Particle
{
public:
	Particle():x(0),y(0),z(0),vx(0),vy(0),vz(0),vx_old(0),vy_old(0),vz_old(0),m(0),id(0){}
	Particle(int idP):x(0),y(0),z(0),vx(0),vy(0),vz(0),vx_old(0),vy_old(0),vz_old(0),m(0),id(idP){ }
	int getId(){ return id; }
private:
	float x,y,z;
	float vx,vy,vz;
	float vx_old,vy_old,vz_old;
	float m;
	int id;
};


// testing
#include <chrono>

int main(int argC, char ** argV)
{
		GraphicsCardSupplyDepot depot;

		// n needs to be integer multiple of pageSize !!!!
		const size_t n = 1024*10000;
		const size_t pageSize=1024;
		const int maxActivePagesPerGpu = 100;

		VirtualMultiArray<Particle> test(n,depot.requestGpus(),pageSize,maxActivePagesPerGpu);
		//VirtualMultiArray<int> test2(n,depot.requestGpus(),pageSize,maxActivePagesPerGpu);

		std::chrono::milliseconds t1 =  std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::system_clock::now().time_since_epoch());
		#pragma omp parallel for
		for(int i=0;i<n;i++)
		{
			// seamless access to i index
			// create a particle with id=i and write to array
			//test2.set(i,int(i));
			test.set(i,Particle(i));
		}

		#pragma omp parallel for
		for(int i=0;i<n;i++)
		{
			// seamless access to i index
			// get particle id and compare to expected value
			//if(test2.get(i)!=i)
			//{
			//	std::cout<<"!!! error at "<<i<<std::endl;
			//}

			if(test.get(i).getId()!=i)
			{
				std::cout<<"!!! error at "<<i<<std::endl;
			}
		}
		std::chrono::milliseconds t2 =  std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::system_clock::now().time_since_epoch());



		auto miliseconds = (t2.count()-t1.count());
		float throughput = (n * sizeof(Particle) * 2/* 1:set + 1:get */ / (miliseconds/1000.0))/(1000000.0);
		std::cout<<"Bandwidth achieved by using virtual array backed by all graphics cards: "<<throughput<<"MB/s"<<std::endl;
		if((throughput>178.1f) && (n==1024*10000) && (pageSize==1024) && (maxActivePagesPerGpu==100))
		{
			std::cout<<"Congrats. Your system is better than this system: GT1030 + K420 + K420 (1 channel 1333MHz ddr3 4GB RAM, FX8150 @ 2.1GHz)"<<std::endl;
			std::cout<<"Your system is "<<(   ((throughput/178.1f)-1.0f)*100.0f )<<"%  faster."<<std::endl;
			std::cout<<"Some other benchmark results from same system:"<<std::endl;

			std::cout<<"testing method                      object size   throughput    page size    cpu threads       total objects     active pages per gpu   RAM   VRAM"<<std::endl;
			std::cout<<"uniform distribution random access  44 bytes      3.1   MB/s    128 objects  8                 100k              4"<<std::endl;
			std::cout<<"uniform distribution random access  4kB           676.5 MB/s    1   object   8                 100k              4"<<std::endl;
			std::cout<<"serial access per thread            4kB           496.4 MB/s    1   object   8                 100k              4"<<std::endl;
			std::cout<<"serial access per thread            4kB           2467.0MB/s    32  objects  8                 100k              4"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      142.9 MB/s    32  objects  8                 100k              4"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      162.3 MB/s    32  objects  8                 1M                4"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      287.0 MB/s    1k  objects  8                 1M                4"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      140.8 MB/s    10k objects  8                 10M               4"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      427.1 MB/s    10k objects  8                 10M               100"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      299.9 MB/s    10k objects  8                 100M              100                    900MB 4.5GB"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      280.5 MB/s    10k objects  8                 100M              50                     600MB 4.5GB"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      249.1 MB/s    10k objects  8                 100M              25                     400MB 4.5GB"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      70.8  MB/s    100kobjects  8                 100M              8                      700MB 4.5GB"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      251.1  MB/s   1k  objects  8                 100M              1000                   1GB   4.5GB"<<std::endl;
			std::cout<<"interleaved threading per object    44 bytes      236.1  MB/s   1k  objects  8                 100M              1000                   1GB   4.5GB"<<std::endl;
			std::cout<<"interleaved threading per object    44 bytes      139.5  MB/s   32  objects  8                 100M              1000                   700MB 4.5GB"<<std::endl;
			std::cout<<"interleaved threading per object    44 bytes      153.6  MB/s   32  objects  8                 100M              100                    500MB 4.5GB"<<std::endl;
			std::cout<<"interleaved threading per object    4kB           2474.0 MB/s   32  objects  8                 1M                5                      400MB 4.2GB"<<std::endl;
		}






	return 0;
}
