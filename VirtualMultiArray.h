/*
 * VirtualMultiArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */


#ifndef VIRTUALMULTIARRAY_H_
#define VIRTUALMULTIARRAY_H_

#include<limits>
#include<vector>
#include<memory>
#include<mutex>
#include <stdexcept>
#include <functional>

// this is for mappedReadWriteAccess --> for linux
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
// windows
#include<memoryapi.h>
#define __restrict__ __restrict
#else
// linux
#include<sys/mman.h>

#endif


#include<thread>

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

	VirtualMultiArray():numDevice(0),pageSize(0),va(nullptr),pageLock(nullptr){};

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
	// usePinnedArraysOnly: pins all active-page buffers to stop OS paging them in/out while doing gpu copies (pageable buffers are slower but need less *resources*)
	VirtualMultiArray(size_t size, std::vector<ClDevice> device, size_t pageSizeP=1024, int numActivePage=50,
			std::vector<int> memMult=std::vector<int>(), MemMult mem=MemMult::UseDefault, const bool usePinnedArraysOnly=true){
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


		openclChannels = gpuCloneMult;

		int ctr = 0;
		std::vector<int> actuallyUsedPhysicalGpuIndex;
		actuallyUsedPhysicalGpuIndex.resize(numPhysicalCard);
		int ctrPhysicalCard =0;
		for(int i=0;i<numPhysicalCard;i++)
		{
			if(gpuCloneMult[i]>0)
			{
				actuallyUsedPhysicalGpuIndex[i]=ctr;
				va.get()[ctr]=VirtualArray<T>(	((extraAllocDeviceIndex>=ctr)?numInterleave:(numInterleave-1)) 	* pageSize,device[i],pageSize,numActivePage,usePinnedArraysOnly);
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
					va.get()[ctr]=VirtualArray<T>(	((extraAllocDeviceIndex>= ctr)?numInterleave:(numInterleave-1)) 	* pageSize,va.get()[index].getContext(),device[i],pageSize,numActivePage,usePinnedArraysOnly);
					ctr++;
					gpuCloneMult[i]--;
					ctrPhysicalCard++;

					work=true;
				}
			}
		}
	}

	int totalGpuChannels()
	{
		int result=0;
		for(int e:openclChannels)
		{
			result += e;
		}
		return result;
	}

	// get data at index
	// index: minimum value=0, maximum value=size-1 but not checked for overflowing/underflowing
	T get(const size_t & index) const{
		const size_t selectedPage = index/pageSize;
		const size_t numInterleave = selectedPage/numDevice;
		const size_t selectedVirtualArray = selectedPage%numDevice;
		const size_t selectedElement = numInterleave*pageSize + (index%pageSize);

		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
		return va.get()[selectedVirtualArray].get(selectedElement);
	}

	// put data to index
	// index: minimum value=0, maximum value=size-1 but not checked for overflowing/underflowing
	void set(const size_t & index, const T & val) const{
		const size_t selectedPage = index/pageSize;
		const size_t numInterleave = selectedPage/numDevice;
		const size_t selectedVirtualArray = selectedPage%numDevice;
		const size_t selectedElement = numInterleave*pageSize + (index%pageSize);

		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
		va.get()[selectedVirtualArray].set(selectedElement,val);
	}

	// read N values starting at index
	// do not use this concurrently with writes if you need all N elements be in same sync
	// because this doesn't guarantee atomicity between all N elements
	// otherwise there is no harm in using writes and this concurrently
	// safe to use with other reads
	// also safe to use with writes as long as user explicitly orders operations with thread-safe pattern for N>pageSize
	std::vector<T> readOnlyGetN(const size_t & index, const size_t & n) const
	{
		std::vector<T> result;
		const size_t selectedPage = index/pageSize;
		const size_t numInterleave = selectedPage/numDevice;
		const size_t selectedVirtualArray = selectedPage%numDevice;
		const size_t modIdx = index%pageSize;
		const size_t selectedElement = numInterleave*pageSize + modIdx;
		if(modIdx + n - 1 < pageSize)
		{
			// full read possible
			std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
			return va.get()[selectedVirtualArray].getN(selectedElement,n);
		}
		else
		{
			size_t nToRead = n;
			size_t maxThisPage = pageSize - modIdx;
			size_t toBeCopied = ((nToRead<maxThisPage)? nToRead: maxThisPage);

			// read this page
			{
				std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
				std::vector<T> part = va.get()[selectedVirtualArray].getN(selectedElement,toBeCopied);
				std::move(part.begin(),part.end(),std::back_inserter(result));
				nToRead -= toBeCopied;
			}

			// repeat for other pages recursively
			if(nToRead>0)
			{
				std::vector<T> part = readOnlyGetN(index+toBeCopied, nToRead);
				std::move(part.begin(),part.end(),std::back_inserter(result));
			}


			return result;
		}
	}


	// write N values starting at index
	// do not use this concurrently with reads if you need all N elements be in same sync
	// because this doesn't guarantee atomicity between all N elements
	// otherwise there is no harm(except for consistency) in using reads/writes and this concurrently as long as thread-safety is given by user
	// there is only page-level thread-safety here (automatically elements within page too) so its not N-level thread-safe when N>pageSize
	// val: element values to write
	// valIndex: index of first element (in val) to copy
	// nVal: number of elements to copy starting at index valIndex
	void writeOnlySetN(const size_t & index, const std::vector<T> & val, const size_t & valIndex=0, const size_t & nVal=(size_t)-1) const
	{
		const size_t n = ((nVal==(size_t)-1)?val.size():nVal);
		const size_t selectedPage = index/pageSize;
		const size_t numInterleave = selectedPage/numDevice;
		const size_t selectedVirtualArray = selectedPage%numDevice;
		const size_t modIdx = index%pageSize;
		const size_t selectedElement = numInterleave*pageSize + modIdx;
		if(modIdx + n - 1 < pageSize)
		{
			// full write possible
			std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
			va.get()[selectedVirtualArray].setN(selectedElement,val,valIndex,n);
		}
		else
		{
			size_t nToWrite = n;
			size_t maxThisPage = pageSize - modIdx;
			size_t toBeCopied = ((nToWrite<maxThisPage)? nToWrite: maxThisPage);

			// write this page
			{
				std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
				va.get()[selectedVirtualArray].setN(selectedElement,val,valIndex, toBeCopied);
				nToWrite -= toBeCopied;
			}

			// repeat for other pages recursively
			if(nToWrite>0)
			{
				writeOnlySetN(index+toBeCopied, val, valIndex+toBeCopied, nToWrite);
			}

		}
	}



	// gpu --> aligned buffer  ---> user function ---> gpu (user needs to take care of thread-safety of whole mapped region)
	// index: start element of buffer (which is 4096-aligned)
	// range: size of aligned buffer with data from virtual array
	// f: user-defined (lambda or another)function to run for processing buffer
	//          the pointer given to user-function is not zero-based (but an offseted version of it to match same index access from outside)
	// pinBuffer: true=pins buffer to stop OS paging it/probably faster data copies, false=no pinning / probably less latency to start function
	// read: true=latest data from virtual array is read into buffer (+latency). default = true
	// write: true=latest data from aligned buffer is written to virtual array (+latency). default = true
	// userPtr: when user needs to evade allocation/free latency on each mapping call, this parameter can be used
	//               if userPtr == nullptr (default), internal allocation is done
	//               if userPtr != nullptr, userPtr is used as internal data copies and is not same address value given to the user function
	//               		array.mappedReadWriteAccess(...[&](T * ptr){  ..compute using ptr[some_index] but not myRawPointer[some_index]..  },myRawPointer);
	//               userPtr needs to be valid between (T *) index and (T *) index + range
	void mappedReadWriteAccess(const size_t index, const size_t range,
								std::function<void(T * const)> f,
								const bool pinBuffer=false,
								const bool read=true,
								const bool write=true,
								T * const userPtr=nullptr) const
	{

		struct TempMem
		{
			TempMem():buf(nullptr){ }
			TempMem(T * __restrict__ const mem):buf(mem){}
			T * __restrict__ const buf;
		};

		


		std::unique_ptr<T,void(*)(void *)> arr(nullptr,free);
		if(nullptr == userPtr)
		{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
// windows
			arr = std::unique_ptr<T,void(*)(void *)>((T *)_aligned_malloc(/* (index*sizeof(T))%4096*/ sizeof(T)*range,4096),_aligned_free);

#else
// linux
			arr = std::unique_ptr<T,void(*)(void *)>((T *)aligned_alloc(/* (index*sizeof(T))%4096*/ 4096,sizeof(T)*range),free);
#endif


		}



		// temporarily allocate aligned buffer (for SIMD operations, faster copies to some devices)
		const TempMem mem((nullptr == userPtr)?(arr.get()):userPtr);


		// lock the buffer so that it will not be paged out by OS during function
		if(pinBuffer)
		{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
// windows
			if(0==VirtualLock(mem.buf,range))
			{
				throw std::invalid_argument("Error: memory pinning failed.");
			}

#else
// linux
			if(ENOMEM==mlock(mem.buf,range))
			{
				throw std::invalid_argument("Error: memory pinning failed.");
			}
#endif

		}


		// get data from gpu
		if(read)
		{
			size_t indexStartPage = index / pageSize;
			size_t indexEndPage = (index+range)/pageSize;
			size_t currentIndex = index;
			size_t remainingRange = range;
			size_t remainingPageElm = pageSize;
			size_t currentRange = 0;
			size_t currentBufElm = 0;
			for(size_t selectedPage = indexStartPage; selectedPage<=indexEndPage; selectedPage++)
			{
				remainingPageElm = pageSize - (currentIndex % pageSize);
 
				currentRange = ((remainingRange<remainingPageElm)? remainingRange: remainingPageElm);

				const size_t selectedVirtualArray = selectedPage%numDevice;
				const size_t numInterleave = selectedPage/numDevice;
				const size_t selectedElement = numInterleave*pageSize + (currentIndex%pageSize);
				if(currentRange>0)
				{
					std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
					va.get()[selectedVirtualArray].copyToBuffer(selectedElement, currentRange, mem.buf+currentBufElm);
				}
				else
				{
					break;
				}
				currentBufElm += currentRange;
				currentIndex += currentRange;
				remainingRange -= currentRange;
			}
		}

		// execute function
		// -index offset matches origins of buffer and virtual array such that buf[i] = va[i] for all elements
		f(mem.buf-index);

		// get data to gpu
		if(write)
		{
			size_t indexStartPage = index / pageSize;
			size_t indexEndPage = (index+range)/pageSize;
			size_t currentIndex = index;
			size_t remainingRange = range;
			size_t remainingPageElm = pageSize;
			size_t currentRange = 0;
			size_t currentBufElm = 0;
			for(size_t selectedPage = indexStartPage; selectedPage<=indexEndPage; selectedPage++)
			{
				remainingPageElm = pageSize - (currentIndex % pageSize);
				currentRange = ((remainingRange<remainingPageElm)? remainingRange: remainingPageElm);

				const size_t selectedVirtualArray = selectedPage%numDevice;
				const size_t numInterleave = selectedPage/numDevice;
				const size_t selectedElement = numInterleave*pageSize + (currentIndex%pageSize);
				if(currentRange>0)
				{
					std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
					va.get()[selectedVirtualArray].copyFromBuffer(selectedElement, currentRange, mem.buf+currentBufElm);
				}
				else
				{
					break;
				}
				currentBufElm += currentRange;
				currentIndex += currentRange;
				remainingRange -= currentRange;
			}
		}

		// unlock pinning
		if(pinBuffer)
		{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
// windows
			VirtualUnlock(mem.buf,range);

#else
// linux
			munlock(mem.buf,range);
#endif

		}
	}

	// using gpu compute power, finds an element with given member value, returns its index
	// obj: object instance
	// member: member of object to be used for searching value
	// indexListMaxSize: maximum number of indices found
	template<typename S>
	std::vector<size_t> find(T & obj, S & member, const int indexListMaxSize=1)
	{
		//throw std::invalid_argument("Error: find method not implemented.");
		size_t adrObj = (size_t) &obj;
		size_t adrMem = (size_t) &member;
		size_t offset = adrMem - adrObj; // how many bytes the member is found after
		std::vector<size_t> results;

		// 1) flush all active pages, expect user not to touch any element during search
		// 2) run search on all gpus on all of their data
		// 3) return index

		std::vector<std::thread> parallel;


		std::mutex mGlobal;
		for(int i=0;i<numDevice;i++)
		{
			parallel.push_back(std::thread([&,i]()
			{
				std::unique_lock<std::mutex> lock(pageLock.get()[i]);
				size_t nump = va.get()[i].getNumP();
				for(size_t pg = 0;pg<nump;pg++)
				{
					va.get()[i].flushPage(pg);
				}

				std::vector<size_t> resultI = va.get()[i].find(offset,member,i,indexListMaxSize);

				{
					const int szResultI = resultI.size();
					for(size_t k = 0; k<szResultI; k++)
					{
						size_t gpuPage = (resultI[k]/pageSize);
						size_t realPage = (gpuPage * numDevice) + i;
						size_t realIndex = (realPage * pageSize) + (resultI[k]%pageSize);
						resultI[k]=realIndex;
					}

					if(szResultI>0)
					{
						std::unique_lock<std::mutex> lock(mGlobal);
						std::move(resultI.begin(),resultI.end(),std::back_inserter(results));
					}
				}
			}));
		}

		for(int i=0;i<numDevice;i++)
		{
			if(parallel[i].joinable())
			{
				parallel[i].join();
			}
		}
		return results;
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
	std::vector<int> openclChannels;
};





#endif /* VIRTUALMULTIARRAY_H_ */
