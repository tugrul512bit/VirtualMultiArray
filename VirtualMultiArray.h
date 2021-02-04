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


#include"ClDevice.h"
#include"VirtualArray.h"

template<typename T>
class VirtualMultiArray
{
public:

// size = integer multiple of pageSize
	VirtualMultiArray(size_t size, std::vector<ClDevice> device, size_t pageSizeP=1024, int numActivePage=50, std::vector<int> memMult=std::vector<int>()){
		int numPhysicalCard = device.size();

		int nDevice = 0;
		std::vector<int> gpuCloneMult;
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


		numDevice=nDevice;
		pageSize=pageSizeP;
		va = std::shared_ptr<VirtualArray<T>>( new VirtualArray<T>[nDevice],[](VirtualArray<T> * ptr){ /* destructor is empty, so placement-new doesn't need it */ delete [] ptr;} );
		pageLock = std::shared_ptr<std::mutex>(new std::mutex[numDevice],[](std::mutex * ptr){delete [] ptr;});


		size_t numPage = size/pageSize;
		size_t numInterleave = (numPage/nDevice) + 1;
		size_t extraAllocDeviceIndex = numPage%nDevice; // 0: all equal, 1: first device extra allocation, 2: second device, ...


		int ctr = 0;
		for(int i=0;i<numPhysicalCard;i++)
		{
			if(gpuCloneMult[i]>0)
			{
				// placement new on dynamically allocated memory, to unlock usage of const members in there without the = operator
				/*va.get()[i]=*/new(va.get()+ctr) VirtualArray<T>(	((extraAllocDeviceIndex>=ctr)?numInterleave:(numInterleave-1)) 	* pageSize,device[i],pageSize,numActivePage);
				ctr++;
				for(int j=0;j<gpuCloneMult[i]-1;j++)
				{
					/*va.get()[i+1]=*/new(va.get()+ctr) VirtualArray<T>(	((extraAllocDeviceIndex>= ctr)?numInterleave:(numInterleave-1)) 	* pageSize,va.get()[ctr-j-1].getContext(),device[i],pageSize,numActivePage);
					ctr++;
				}
			}
		}

	}

/*
 * o o o o o o o o o o o (11)   selectedPage
 * a e i a e i a e i a e (3)  a e (2) selectedPage - (selectedPage/numDevice)*numDevice    [1]
 * o o o o o o o o o o   (10)   selectedPage
 * a e i a e i a e i a   (3)  a   (1) selectedPage - (selectedPage/numDevice)*numDevice    [0]
*/

	T get(const size_t & index){
		const size_t selectedPage = index/pageSize;
		const size_t numInterleave = selectedPage/numDevice;
		const size_t selectedVirtualArray = selectedPage%numDevice;

		const size_t selectedElement = numInterleave*pageSize + (index%pageSize);

		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);

		return va.get()[selectedVirtualArray].get(selectedElement);
	}

	void set(const size_t & index, const T & val){
		const size_t selectedPage = index/pageSize;
		const size_t numInterleave = selectedPage/numDevice;
		const size_t selectedVirtualArray = selectedPage%numDevice;

		const size_t selectedElement = numInterleave*pageSize + (index%pageSize);
		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);

		va.get()[selectedVirtualArray].set(selectedElement,val);
	}
~VirtualMultiArray(){}
private:
std::shared_ptr<VirtualArray<T>> va;

size_t pageSize;
size_t numDevice;
std::shared_ptr<std::mutex> pageLock;
};





#endif /* VIRTUALMULTIARRAY_H_ */
