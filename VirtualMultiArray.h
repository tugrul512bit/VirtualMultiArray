/*
 * VirtualMultiArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

/*
 * A virtual array that uses graphics cards as backing store, optimized with an N-way LRU cache per physical graphics card
 * Page: a chunk of data that is traded between RAM and VRAM to keep necessary content close to user
 * Page Size: number of elements in a page / cache line size of LRU
 * Memory Multiplier: VRAM usage ratio per graphics card, also the number of LRUs per graphics card, also the number of parallel data channels per card
 * 		Every virtual gpu is produced from a physical gpu and is given 1 LRU cache, 1 OpenCL command queue, X amount of VRAM
 * 		So that I/O can be overlapped between other cards or other virtual gpus of same physical gpu
 * Active Page: a chunk of data that is on RAM within LRU cache (with a computed physical index)
 * Frozen Page: a chunk of data in VRAM (with a computed virtual index)
 * Number of Active Pages = number of cache lines for each LRU
 *
 *
 * How to hide I/O latency: Using more threads than CPU logical cores when accessing elements with any method except uncached versions
 * 		Currently only Linux support for the I/O latency hiding
 * How to reduce average latency per element for sequential access: use bulk read/write (readOnlyGetN, writeOnlySetN,mappedReadWriteAccess)
 * 		Also allocate more(and larger) active pages (cache lines) for higher amounts of threads accessing concurrently
 * How to increase throughput for random-access: decrease page size (cache line size), increase number of active pages (cache lines)
 * How to decrease latency for random-access: use getUncached/setUncached methods between streamStart/streamStop commands.
 *
 */

#ifndef VIRTUALMULTIARRAY_H_
#define VIRTUALMULTIARRAY_H_

#include<limits>
#include<vector>
#include<memory>
#include<mutex>
#include <stdexcept>
#include <functional>


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
	//			6) "number of LRU cache per physical card" ==> each virtual card caches every k-th page in interleaved form with other virtual cards
	//				for example: {1,1,1} with numActivePage=5 means pages are cached in this order: (c=cache) c1 c2 c3 c1 c2 c3 c1 c2 c3 c1 c2 c3 c1 c2 c3
	//																										  |        |         |       |         |
	//																										c1: LRU cache that serves pages at index%3 = 0
	//																										c2: LRU cache at index%3 = 1
	//																										c3: LRU cache at index%3 = 2
	//          default: {4,4,...,4} all cards are given 4 data channels so total RAM usage becomes this: { nGpu * 4 * pageSize * sizeof(T) * numActivePage }
	//				which also means 4 independent LRU caches per physical card: c1_1 c2_1 c3_1 c1_2 c2_2 c3_2 .... c1_4 c2_4 c3_4 c1_1 c2_1 ...
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
			auto info = std::string("Error :number of pages(")+std::to_string(size/pageSizeP)+std::string(") must be equal to or greater than number of virtual gpu instances(")+std::to_string(nDevice)+std::string(").");
			auto debug3 = std::string("Total devices: ")+std::to_string(nDevice)+std::string("\r\n");
			auto debug4 = std::string("Total pages: ")+std::to_string(size/pageSizeP)+std::string("\r\n");
			throw std::invalid_argument(debug3+debug4+info);
		}



		numDevice=nDevice;
		pageSize=pageSizeP;
		va = std::shared_ptr<VirtualArray<T>>( new VirtualArray<T>[numDevice],[&](VirtualArray<T> * ptr){

			delete [] ptr;
		});
		pageLock = std::shared_ptr<LMutex>(new LMutex[numDevice],[](LMutex * ptr){delete [] ptr;});


		size_t numPage = size/pageSize;
		size_t numInterleave = (numPage/nDevice) + 1;
		size_t extraAllocDeviceIndex = numPage%nDevice; // 0: all equal, 1: first device extra allocation, 2: second device, ...


		openclChannels = gpuCloneMult;

		{
			int total = 0;
			for(const auto& e:openclChannels)
			{
				total+=e;
			}
			if(total * numActivePage > size/pageSizeP)
			{
				auto info = std::string("Error: total number of active pages (")+std::to_string(total * numActivePage)+std::string(")")+
						std::string(" required to be less than or equal to total pages(")+std::to_string(size/pageSizeP)+std::string(")");
				auto debug1 = std::string("Active page per virtual gpu: ")+std::to_string(numActivePage)+std::string("\r\n");
				auto debug2 = std::string("Virtual gpus: ")+std::to_string(total)+std::string("\r\n");
				auto debug3 = std::string("Total active pages: ")+std::to_string(total * numActivePage )+std::string("\r\n");
				auto debug4 = std::string("Total pages: ")+std::to_string(size/pageSizeP)+std::string("\r\n");
				throw std::invalid_argument(debug1+debug2+debug3+debug4+info);
			}
		}
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

		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray].m);
		return va.get()[selectedVirtualArray].get(selectedElement);
	}

	// put data to index
	// index: minimum value=0, maximum value=size-1 but not checked for overflowing/underflowing
	void set(const size_t & index, const T & val) const{
		const size_t selectedPage = index/pageSize;
		const size_t numInterleave = selectedPage/numDevice;
		const size_t selectedVirtualArray = selectedPage%numDevice;
		const size_t selectedElement = numInterleave*pageSize + (index%pageSize);

		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray].m);
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
			std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray].m);
			return va.get()[selectedVirtualArray].getN(selectedElement,n);
		}
		else
		{
			size_t nToRead = n;
			size_t maxThisPage = pageSize - modIdx;
			size_t toBeCopied = ((nToRead<maxThisPage)? nToRead: maxThisPage);

			// read this page
			{
				std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray].m);
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
			std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray].m);
			va.get()[selectedVirtualArray].setN(selectedElement,val,valIndex,n);
		}
		else
		{
			size_t nToWrite = n;
			size_t maxThisPage = pageSize - modIdx;
			size_t toBeCopied = ((nToWrite<maxThisPage)? nToWrite: maxThisPage);

			// write this page
			{
				std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray].m);
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
					std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray].m);
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
					std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray].m);
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

	// get data directly from vram, bypassing LRU cache
	// if streamStart() was not called before uncached stream-read commands, then uncached data will not be guaranteed to be updated
	// not thread-safe for overlapping regions
	T getUncached(const size_t index) const
	{
		const size_t selectedPage = index/pageSize;
		const size_t numInterleave = selectedPage/numDevice;
		const size_t selectedVirtualArray = selectedPage%numDevice;
		const size_t selectedElement = numInterleave*pageSize + (index%pageSize);
		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray].m);
		return va.get()[selectedVirtualArray].getUncached(selectedElement);
	}

	// set data directly to vram, bypassing LRU cache
	// if streamStop() was not called after uncached stream-write commands, then cached data after this will not be guaranteed to be updated
	// not thread-safe for overlapping regions
	void setUncached(const size_t index, const T& val ) const
	{
		const size_t selectedPage = index/pageSize;
		const size_t numInterleave = selectedPage/numDevice;
		const size_t selectedVirtualArray = selectedPage%numDevice;
		const size_t selectedElement = numInterleave*pageSize + (index%pageSize);
		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray].m);
		va.get()[selectedVirtualArray].setUncached(selectedElement,val);
	}

	// writes all edited active pages to vram
	// resets all active pages
	// use this before a series of uncached reads/writes
	void streamStart()
	{
		std::vector<std::thread> parallel;
		for(int i=0;i<numDevice;i++)
		{
			parallel.push_back(std::thread([&,i]()
			{
				std::unique_lock<std::mutex> lock(pageLock.get()[i].m);
				size_t nump = va.get()[i].getNumP();
				for(size_t pg = 0;pg<nump;pg++)
				{
					va.get()[i].flushPage(pg);
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
	}

	// reloads all edited frozen pages from vram to active pages (LRU cache)
	// resets all active pages
	// overwrites any cached writes happened before
	// use this after a series of uncached reads/writes
	void streamStop()
	{
		std::vector<std::thread> parallel;
		for(int i=0;i<numDevice;i++)
		{
			parallel.push_back(std::thread([&,i]()
			{
				std::unique_lock<std::mutex> lock(pageLock.get()[i].m);
				size_t nump = va.get()[i].getNumP();
				for(size_t pg = 0;pg<nump;pg++)
				{
					va.get()[i].reloadPage(pg);
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
				std::unique_lock<std::mutex> lock(pageLock.get()[i].m);
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
	std::shared_ptr<LMutex> pageLock;
	std::vector<int> openclChannels;
};





#endif /* VIRTUALMULTIARRAY_H_ */
