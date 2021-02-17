/*
 * PcieBandwidthBenchmarker.h
 *
 *  Created on: Feb 8, 2021
 *      Author: tugrul
 */

#ifndef PCIEBANDWIDTHBENCHMARKER_H_
#define PCIEBANDWIDTHBENCHMARKER_H_


#include "GraphicsCardSupplyDepot.h"
#include "VirtualMultiArray.h"

#include<vector>
#include<thread>
#include<cmath>
template <class F>
void parallelFor(const int N, const F  f) {


	std::vector<std::thread> thr;

	for(int i=0; i<N; i++) {
		thr.push_back(std::thread(f,i));
	}
	for(int i=0; i<N; i++) {
		thr[i].join();
	}
}

class PcieBandwidthBenchmarker
{
private:
	struct Obj
	{
		char test[1024*128];
	};
	std::vector<float> bandwidthOptimizedMultipliers;
	float minBw;
public:

	// benchmarks system to find pcie performances
	// megabytesPerCardAllowedForBenchmarking: each card is given this amount of data (in MB unit) during benchmark
	PcieBandwidthBenchmarker(int megabytesPerCardAllowedForBenchmarking=128)
	{
		GraphicsCardSupplyDepot depot;
		const size_t n = megabytesPerCardAllowedForBenchmarking*4; // for each test array (2 arrays below)
		const size_t pageSize=1;
		const int maxActivePagesPerGpu = 1;
		auto gpus = depot.requestGpus();
		auto findBandwidth = [&](int selectDevice, std::vector<ClDevice> & devList)
		{
			std::vector<int> bwList;
			for(int i=0;i<devList.size();i++)
			{
				bwList.push_back(i==selectDevice?4:0);
			}
			VirtualMultiArray<Obj> data1(n,devList,pageSize,maxActivePagesPerGpu,bwList);
			VirtualMultiArray<Obj> data2(n,devList,pageSize,maxActivePagesPerGpu,bwList);
			const int numThr = std::thread::hardware_concurrency();
			std::chrono::milliseconds t1 =  std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::system_clock::now().time_since_epoch());
			parallelFor(numThr,[&](int idx){
				for(size_t j=0;j<n;j++)
				{
					if(idx==(j%numThr))
					{
						data1[j]=data2[j];
					}
				}
			});
			std::chrono::milliseconds t2 =  std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::system_clock::now().time_since_epoch());
			return 1.0/(t2.count()-t1.count());
		};

		// heating cpu/pcie
		for(int i=0;i<gpus.size();i++)
		{
			findBandwidth(i,gpus);
		}

		// benchmark
		std::vector<float> bwMeasured;
		minBw=1000000000.0f;
		for(int i=0;i<gpus.size();i++)
		{
			bwMeasured.push_back(findBandwidth(i,gpus));
			if(minBw>bwMeasured[i])
				minBw=bwMeasured[i];
		}


		bandwidthOptimizedMultipliers = bwMeasured;
	}

	// gets multipliers for each physical card that maximizes bandwidth
	// minimumMultiplierNeeded: slowest-accessed card is given this amount of channels (virtual cards)
	//                          other cards receive higher multipliers depending on their relative communication performance
	std::vector<int> bestBandwidth(int minimumMultiplierNeeded){
		std::vector<int> result;
		float mul = 1.0 / minBw;
		for(int i=0;i<bandwidthOptimizedMultipliers.size();i++)
		{
			result.push_back(std::floor(bandwidthOptimizedMultipliers[i] * mul*minimumMultiplierNeeded));
		}
		return result;
	}
};



#endif /* PCIEBANDWIDTHBENCHMARKER_H_ */
