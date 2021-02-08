/*
 * VirtualMultiArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */


#ifndef VIRTUALMULTIARRAY_H_
#define VIRTUALMULTIARRAY_H_

#include<vector>
#include<memory>
#include<mutex>
#include <stdexcept>

#include"ClDevice.h"
#include"VirtualArray.h"

template<typename T>
class VirtualMultiArray
{
public:

	// to choose
	enum MemMult {

		// equal data distribution on all gpu instances
		// also uses user-defined values when memMult parameter is not empty
		UseDefault=0,

		// distribution ratios are equal to vram sizes of cards, (uses integer GB values, 2GB = 2, 1.9 GB = 1, 2.1GB = 2)
		UseVramRatios=1,

		// distribution ratio that reflects pci-e specs of cards to maximize bandwidth
		// not-implemented
		UsePcieRatios=2
	};


	// creates virtual array on a list of devices
	// size: number of array elements (needs to be integer-multiple of pageSize)
	// device: list of (physical) graphics cards that are used to generate multiple virtual cards to overlap multiple operations(device to host, host-to-device data copies) on them
	// pageSizeP: number of elements per page (should not be too big for pinned-array limitations, should not be too small for pcie bandwidth efficiency)
	// numActivePage: number of RAM-backed pages per (virtual) graphics card
	// memMult: 1) "number of virtual graphics cards per physical card".  {1,2,3} means first card will be just physical, second card will be dual v-cards, last one becomes 3 v-cards
	//          2) "ratio of VRAM usage per physical card". {1,2,3} means a 6GB array will be distributed as 1GB,2GB,3GB on 3 cards
	//          3) "average multithreaded usage limit per physical card". {1,2,3} means no data-copy-overlap on first card, 2 copies in-flight on second card, 3 copies concurrently on 3rd card
	//          4) to disable a card, give it 0. {1,0,5} means first card is physical, second card is not used, third card will have intense pcie activity and VRAM consumption
	//          5) every value in vector means extra RAM usage.
	//          default: {4,4,...,4} all cards are given 4 data channels so total RAM usage becomes this: { nGpu * 4 * pageSize * sizeof(T) * numActivePage }
	// mem:
	// 			UseDefault = uses user-input values from "memMult" parameter,
	//			UseVramRatios=allocates from gpus in tune with their vram sizes to maximize array capacity,
	//			UsePcieRatios(not implemented)=allocation ratio for pcie specs to maximize bandwidth
	VirtualMultiArray(size_t size, std::vector<ClDevice> device, size_t pageSizeP=1024, int numActivePage=50, std::vector<int> memMult=std::vector<int>(), MemMult mem=MemMult::UseDefault){
		int numPhysicalCard = device.size();

		int nDevice = 0;
		std::vector<int> gpuCloneMult;

		if(MemMult::UseDefault==mem)
		{
			if(memMult.size()==0)
			{
				// auto distribution rate (all graphics cards take equal data)
				for(int i=0;i<numPhysicalCard;i++)
				{
					gpuCloneMult.push_back(4); // every card becomes 4 virtual cards (for overlapping data transfers of opposite directions, performance)
					nDevice+=4;

				}
			}
			else
			{
				// user-defined memory usage per card, by multiplier-per-card
				for(int i=0;i<numPhysicalCard;i++)
				{
					if(i<memMult.size())
					{


						gpuCloneMult.push_back(memMult[i]); // every card becomes 4*n virtual cards (for overlapping data transfers of opposite directions, performance)
						nDevice += memMult[i];


					}
					else
					{
						gpuCloneMult.push_back(4); // every card becomes 4*n virtual cards (for overlapping data transfers of opposite directions, performance)
						nDevice += 4;

					}
				}
			}
		}
		else if (MemMult::UseVramRatios==mem)
		{

			if(memMult.size()==0)
			{
				for(int i=0;i<numPhysicalCard;i++)
				{
					int vram = device[i].vramSize();
					gpuCloneMult.push_back(vram);
					nDevice+=vram;

				}
			}
			else
			{
				// user-defined memory usage per card, only taking 0 as disabler for a card
				for(int i=0;i<numPhysicalCard;i++)
				{
					int vram = device[i].vramSize();
					// user-unspecified gpus are assumed non-zero and over-specified elements are ignored
					gpuCloneMult.push_back((memMult[i]>0)?vram:0);
					nDevice += ((memMult[i]>0)?vram:0);
				}
			}

		}
		else
		{
			throw std::invalid_argument("Not implemented: MemMult::UsePcieRatios");
		}

		if((size/pageSizeP)*pageSizeP !=size)
		{
			throw std::invalid_argument(std::string("Error :number of elements(")+std::to_string(size)+std::string(") need to be integer-multiple of page size(")+std::to_string(pageSizeP)+std::string(")."));
		}

		if(nDevice>size/pageSizeP)
		{
			throw std::invalid_argument(std::string("Error :number of pages(")+std::to_string(size/pageSizeP)+std::string(") must be equal to or greater than number of virtual gpu instances(")+std::to_string(nDevice)+std::string(")."));
		}



		numDevice=nDevice;
		pageSize=pageSizeP;
		va = std::shared_ptr<VirtualArray<T>>( new VirtualArray<T>[numDevice],[&](VirtualArray<T> * ptr){

			delete [] ptr;
		});
		pageLock = std::shared_ptr<std::mutex>(new std::mutex[numDevice],[](std::mutex * ptr){delete [] ptr;});


		size_t numPage = size/pageSize;
		size_t numInterleave = (numPage/nDevice) + 1;
		size_t extraAllocDeviceIndex = numPage%nDevice; // 0: all equal, 1: first device extra allocation, 2: second device, ...



		int ctr = 0;
		std::vector<int> actuallyUsedPhysicalGpuIndex;
		actuallyUsedPhysicalGpuIndex.resize(numPhysicalCard);
		int ctrPhysicalCard =0;
		for(int i=0;i<numPhysicalCard;i++)
		{
			if(gpuCloneMult[i]>0)
			{
				actuallyUsedPhysicalGpuIndex[i]=ctr;
				va.get()[ctr]=VirtualArray<T>(	((extraAllocDeviceIndex>=ctr)?numInterleave:(numInterleave-1)) 	* pageSize,device[i],pageSize,numActivePage);
				ctr++;
				gpuCloneMult[i]--;
				ctrPhysicalCard++;
			}
		}
		bool work = true;

		while(work)
		{
			ctrPhysicalCard = 0;
			work=false;
			for(int i=0;i<numPhysicalCard;i++)
			{
				if(gpuCloneMult[i]>0)
				{

					int index = actuallyUsedPhysicalGpuIndex[i];
					va.get()[ctr]=VirtualArray<T>(	((extraAllocDeviceIndex>= ctr)?numInterleave:(numInterleave-1)) 	* pageSize,va.get()[index].getContext(),device[i],pageSize,numActivePage);
					ctr++;
					gpuCloneMult[i]--;
					ctrPhysicalCard++;

					work=true;
				}
			}
		}
	}


	// get data at index
	// index: minimum value=0, maximum value=size-1 but not checked for overflowing/underflowing
	T get(const size_t & index){
		const size_t selectedPage = index/pageSize;
		const size_t numInterleave = selectedPage/numDevice;
		const size_t selectedVirtualArray = selectedPage%numDevice;

		const size_t selectedElement = numInterleave*pageSize + (index%pageSize);

		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);

		return va.get()[selectedVirtualArray].get(selectedElement);
	}

	// put data to index
	// index: minimum value=0, maximum value=size-1 but not checked for overflowing/underflowing
	void set(const size_t & index, const T & val){
		const size_t selectedPage = index/pageSize;
		const size_t numInterleave = selectedPage/numDevice;
		const size_t selectedVirtualArray = selectedPage%numDevice;

		const size_t selectedElement = numInterleave*pageSize + (index%pageSize);
		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);

		va.get()[selectedVirtualArray].set(selectedElement,val);
	}

	class SetterGetter
	{
	public:
		SetterGetter():ptr(nullptr),idx(0){}

		SetterGetter(size_t i, VirtualMultiArray<T> * p):idx(i),ptr(p){}

		void operator = (T val)  { ptr->set(idx,val); }
		operator T(){ return ptr->get(idx); }

		void operator = (SetterGetter sg){ ptr->set(idx,sg.ptr->get(sg.idx));  }
	private:
		size_t  idx;
		VirtualMultiArray<T> * ptr;

	};

	SetterGetter operator [](const size_t id) { return SetterGetter(id,this); }
	T operator [](const size_t id) const { return get(id); }


	~VirtualMultiArray(){}
private:
	size_t numDevice;
	size_t pageSize;
	std::shared_ptr<VirtualArray<T>> va;
	std::shared_ptr<std::mutex> pageLock;
};





#endif /* VIRTUALMULTIARRAY_H_ */
