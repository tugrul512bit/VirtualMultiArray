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
VirtualMultiArray(size_t size, std::vector<ClDevice> device, size_t pageSizeP=1024, int numActivePage=50){


	int nDevice = device.size()*4;
	numDevice=nDevice;
	pageSize=pageSizeP;
	va = std::shared_ptr<VirtualArray<T>>( new VirtualArray<T>[nDevice],[](VirtualArray<T> * ptr){delete [] ptr;} );
	pageLock = std::shared_ptr<std::mutex>(new std::mutex[numDevice],[](std::mutex * ptr){delete [] ptr;});

	// calc interleave offset
	std::vector<size_t> gpuPageIndex;
	for(int i=0;i<nDevice;i++)
	{
		gpuPageIndex.push_back(0);
	}

	size_t numPage = size/pageSize;
	for(size_t i=0;i<numPage;i++)
	{
		mGpuIndex.push_back(gpuPageIndex[i%nDevice]++);
	}

	for(int i=0;i<nDevice;i+=4)
	{
		va.get()[i]=VirtualArray<T>(	gpuPageIndex[i] 	* pageSize,device[i/4],pageSize,numActivePage);

		va.get()[i+1]=VirtualArray<T>(	gpuPageIndex[i+1] 	* pageSize,va.get()[i].getContext(),device[i/4],pageSize,numActivePage);

		va.get()[i+2]=VirtualArray<T>(	gpuPageIndex[i+2] 	* pageSize,va.get()[i].getContext(),device[i/4],pageSize,numActivePage);

		va.get()[i+3]=VirtualArray<T>(	gpuPageIndex[i+3] 	* pageSize,va.get()[i].getContext(),device[i/4],pageSize,numActivePage);
	}

}

	T get(size_t index){

		size_t selectedPage = index/pageSize;
		size_t selectedVirtualArray = selectedPage%numDevice;
		size_t selectedElement = mGpuIndex[selectedPage]*pageSize + (index%pageSize);
		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
		return va.get()[selectedVirtualArray].get(selectedElement);
	}

	void set(size_t index, T val){

		size_t selectedPage = index/pageSize;
		size_t selectedVirtualArray = selectedPage%numDevice;
		size_t selectedElement = mGpuIndex[selectedPage]*pageSize + (index%pageSize);
		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
		va.get()[selectedVirtualArray].set(selectedElement,val);
	}
~VirtualMultiArray(){}
private:
std::shared_ptr<VirtualArray<T>> va;
std::vector<size_t> mGpuIndex;
size_t pageSize;
size_t numDevice;
std::shared_ptr<std::mutex> pageLock;
};





#endif /* VIRTUALMULTIARRAY_H_ */
