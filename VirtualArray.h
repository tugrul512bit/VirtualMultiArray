/*
 * VirtualArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef VIRTUALARRAY_H_
#define VIRTUALARRAY_H_

#include<memory>
#include<vector>
#include <stdexcept>
#include"ClPlatform.h"
#include"ClDevice.h"
#include"ClContext.h"
#include"ClCommandQueue.h"
#include"ClArray.h"
#include"ClCompute.h"
#include"Page.h"
#include<CL/cl.h>

// this is a non-threadsafe single-graphics-card using virtual array
template<typename T>
class VirtualArray
{
public:
	// don't use this
	VirtualArray():sz(0),szp(0),nump(0),computeFind(nullptr){}

	// for generating a physical-card based virtual array
	// takes a single virtual graphics card, size(in number of objects), page size(in number of objects), active pages (number of pages in interleaved order for caching)
	// sizeP: number of elements of array (VRAM backed)
	// device: opencl wrapper that contains only 1 graphics card
	// sizePageP: number of elements of each page (bigger pages = more RAM used)
	// numActivePageP: parameter for number of active pages (in RAM) for interleaved access caching (instead of LRU, etc) with less book-keeping overhead
	VirtualArray(const size_t sizeP,  ClDevice device, const int sizePageP=1024, const int numActivePageP=50, const bool usePinnedArraysOnly=true):sz(sizeP),szp(sizePageP),nump(numActivePageP){
		computeFind = nullptr;
		dv = std::make_shared<ClDevice>();
		*dv=device.generate()[0];
		ctx= std::make_shared<ClContext>(*dv,0);

		q= std::make_shared<ClCommandQueue>(*ctx,*dv);
		gpu= std::make_shared<ClArray<T>>(sz,*ctx);
		cpu= std::shared_ptr<Page<T>>(new Page<T>[nump],[](Page<T> * ptr){delete [] ptr;});
		for(int i=0;i<nump;i++)
		{
			cpu.get()[i]=Page<T>(szp,*ctx,*q,usePinnedArraysOnly);
		}
	}

	// for generating a virtual-card based virtual array
	// takes a single virtual graphics card, size(in number of objects), page size(in number of objects), active pages (number of pages in interleaved order for caching)
	// context: a shared context with other virtual cards (or a physical card)
	// sizeP: number of elements of array (VRAM backed)
	// device: opencl wrapper that contains only 1 graphics card
	// sizePageP: number of elements of each page (bigger pages = more RAM used)
	// numActivePageP: parameter for number of active pages (in RAM) for interleaved access caching (instead of LRU, etc) with less book-keeping overhead
	VirtualArray(const size_t sizeP, ClContext context, ClDevice device, const int sizePageP=1024, const int numActivePageP=50, const bool usePinnedArraysOnly=true):sz(sizeP),szp(sizePageP),nump(numActivePageP){
		computeFind = nullptr;
		dv = std::make_shared<ClDevice>();
		*dv=device.generate()[0];
		ctx= context.generate();
		q= std::make_shared<ClCommandQueue>(*ctx,*dv);
		gpu= std::make_shared<ClArray<T>>(sz,*ctx);
		cpu= std::shared_ptr<Page<T>>(new Page<T>[nump],[](Page<T> * ptr){delete [] ptr;});
		for(int i=0;i<nump;i++)
		{
			cpu.get()[i]=Page<T>(szp,*ctx,*q,usePinnedArraysOnly);
		}
	}

	// array access for reading an element at an index
	T get(const size_t & index)
	{
		const size_t selectedPage = index/szp;
		const int selectedActivePage = selectedPage % nump;
		Page<T> * sel = cpu.get()+selectedActivePage;
		if(sel->getTargetGpuPage()!=selectedPage)
		{
			updatePage(sel, selectedPage);
			sel->reset();			
		}
		return sel->get(index - selectedPage * szp);
	}

	// array access for writing to an element at an index
	// val: value to write to array
	void set(const size_t & index, const T & val)
	{
		const size_t selectedPage = index/szp;
		const int selectedActivePage = selectedPage % nump;
		Page<T> * sel = cpu.get()+selectedActivePage;
		if(sel->getTargetGpuPage()!=selectedPage)
		{
			updatePage(sel, selectedPage);
		}
		sel->edit(index - selectedPage * szp, val);
		sel->markAsEdited();

	}



	// array access for reading multiple elements beginning at an index
	// n is guaranteed to no overflow by VirtualMultiArray's readOnlyGetN
	std::vector<T> getN(const size_t & index,int n)
	{
		std::vector<T> result;
		const size_t selectedPage = index/szp;
		const int selectedActivePage = selectedPage % nump;
		Page<T> * sel = cpu.get()+selectedActivePage;
		if(sel->getTargetGpuPage()!=selectedPage)
		{
			updatePage(sel, selectedPage);
			sel->reset();			
		}
		return sel->getN(index - selectedPage * szp, n);
	}


	// array access for writing to an element at an index
	// val: value to write to array
	void setN(const size_t & index, const std::vector<T> & val, const size_t & valIndex, const size_t n)
	{
		const size_t selectedPage = index/szp;
		const int selectedActivePage = selectedPage % nump;
		Page<T> * sel = cpu.get()+selectedActivePage;
		if(sel->getTargetGpuPage()!=selectedPage)
		{
			updatePage(sel, selectedPage);
		}
		sel->editN(index - selectedPage * szp, val, valIndex, n);
		sel->markAsEdited();
	}




	// array access for reading many elements directly through raw pointer
	// without allocating any buffer
	// index: starting element
	// range: number of elements to read into out parameter
	// out: target array to be filled with data
	void copyToBuffer(const size_t & index, const size_t & range, T * const out)
	{
		const size_t selectedPage = index/szp;
		const int selectedActivePage = selectedPage % nump;
		Page<T> * sel = cpu.get()+selectedActivePage;
		if(sel->getTargetGpuPage()!=selectedPage)
		{
			updatePage(sel, selectedPage);
			sel->reset();		
		}
		sel->readN(out, index - selectedPage * szp, range);
	}

	// array access for reading many elements directly through raw pointer
	// without allocating any buffer
	// index: starting element
	// range: number of elements to read into out parameter
	// out: target array to be filled with data
	void copyFromBuffer(const size_t & index, const size_t & range, T * const in)
	{
		const size_t selectedPage = index/szp;
		const int selectedActivePage = selectedPage % nump;
		Page<T> * sel = cpu.get()+selectedActivePage;
		if(sel->getTargetGpuPage()!=selectedPage)
		{
			updatePage(sel, selectedPage);		
		}
		sel->writeN(in, index - selectedPage * szp, range);
		sel->markAsEdited();
	}

	// a sub-operation of VirtualMultiArray::find() to do fully gpu-accelerated element search
	void flushPage(size_t pageIdx)
	{
		Page<T> * sel = cpu.get()+pageIdx;
		if(sel->isEdited())
		{
			cl_int err=clEnqueueWriteBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T)*(sel->getTargetGpuPage())* szp,sizeof(T)* szp,sel->ptr(),0,nullptr,nullptr);
			if(CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: flush page ");
			}
			clFinish(q->getQueue());
		}
		sel->reset();
	}

	// opencl compute test
	template<typename S>
	std::vector<size_t> find(int memberOffset, S memberValue, const int vaId, const int foundIdMaxListSize = 1)
	{
		int foundIdListSize = foundIdMaxListSize;
		// kernel parameter data
		int objSizeTmp = sizeof(T);
		int ofs = memberOffset;
		S val = memberValue;
		int memberSizeTmp = sizeof(S);
		std::vector<size_t> found(foundIdListSize+1 /* first element is atomic counter in gpu */ ,0);

		// lazy init opencl compute resources
		if(computeFind==nullptr)
		{
			computeFind = std::unique_ptr<ClCompute>(new ClCompute(*ctx,*dv,std::string(R"(
                     #define __N__ ( )")+std::to_string(sz)+std::string(R"(UL )
                     #pragma OPENCL EXTENSION cl_khr_int64_base_atomics : enable

	                 __kernel void )")+std::string(R"(find)")+std::to_string(vaId)+ std::string(R"(
	                 (                   __global unsigned char * memberVal,
	                                     __global int * memberOfs, 
	                                     volatile __global size_t * findList, 
	                                     __global int * objSize, 
	                                     __global int * memberSize,
	                                     __global unsigned char * arr,
                                         __global int * findListSize)
					{                                                                      
					   size_t id=get_global_id(0);
                       if(id>=__N__) return;
	                   size_t oSize = *objSize;
	                   size_t mSize = *memberSize;
 
                       int sz = *findListSize;                  
	                   int valCtr = 0;
	                   int cmpCtr = 0;

                       /* optimized data load for bigger member sizes */
                       size_t remaining = mSize % 4; // loading by 4-byte unsigned integers

	                   size_t oSizeI0= oSize*id + *memberOfs;
	                   size_t oSizeI1= oSizeI0 + mSize; 
 
                       // if multiple of int size and aligned to 32bit                      
                       if((remaining == 0) && ((((size_t)arr)%4) == 0) && ((((size_t)(memberVal)  )%4)==0))
                       {
						   for(size_t i=oSizeI0; i<oSizeI1; i+=4)
						   {
							   cmpCtr+=(4*(*((__global int*)(arr+i)) == *((__global int*)(memberVal+valCtr))));                       
							   valCtr+=4;
						   }
                       }
                       else
                       {

						   for(size_t i=oSizeI0; i<oSizeI1; i++)
						   {
							   cmpCtr+=(arr[i] == memberVal[valCtr]);                       
							   valCtr++;
						   }
                       }     
                                
					   if(cmpCtr == mSize)
					   {
	                        
							size_t adr = atom_add(&findList[0],(size_t)1);                 
	                        if((adr+1)<=sz)
							   findList[adr+1]=id;
	                        //mem_fence(CLK_GLOBAL_MEM_FENCE);
					   }             
					}                                                                      )"),std::string("find")+std::to_string(vaId)));

			computeFind->addParameter(*ctx,"member value",sizeof(S),0);
			computeFind->addParameter(*ctx,"member offset",64,1);
			computeFind->addParameter(*ctx,"found index list",(foundIdListSize + 1)*sizeof(size_t),2);
			computeFind->addParameter(*ctx,"object size",64,3);
			computeFind->addParameter(*ctx,"member size",64,4);
			computeFind->addParameter(*ctx,"data buffer",64,5,gpu->getMemPtr());
			computeFind->addParameter(*ctx,"found index list size",64,6);
			computeFind->setKernelArgs();
		}

		// if search member is different sized than the last one
		if(memberSizeTmp!=computeFind->getArgSizeBytes("member value"))
		{
			// allocate new resources
			computeFind->addParameter(*ctx,"member value",sizeof(S),0);
			computeFind->setKernelArgs(0);
		}

		// if search result list length is not same as the last one
		if(((foundIdListSize+1)*sizeof(size_t))!=computeFind->getArgSizeBytes("found index list"))
		{
			// allocate new resources
			computeFind->addParameter(*ctx,"found index list",(foundIdListSize + 1)*sizeof(size_t),2);
			computeFind->setKernelArgs(2);
		}

		// set argument values of kernel
		computeFind->setArgValueAsync("member offset",*q,&ofs);
		computeFind->setArgValueAsync("member value",*q,&val);
		computeFind->setArgValueAsync("found index list",*q,found.data());
		computeFind->setArgValueAsync("object size",*q,&objSizeTmp);
		computeFind->setArgValueAsync("member size",*q,&memberSizeTmp);
		computeFind->setArgValueAsync("found index list size",*q,&foundIdListSize);

		// run kernel
		computeFind->runAsync(*q,sz+256-(sz%256),256);

		// read find list
		computeFind->getArgValueAsync("found index list",*q,*found.data());

		computeFind->sync(*q);

		size_t numFound = found[0];
		if(numFound>foundIdListSize)
			numFound=foundIdListSize;

		// empty cells deleted
		if(numFound>0)
		{
			return std::vector<size_t>(found.begin()+1,found.begin()+1+numFound);
		}
		else
		{
			return std::vector<size_t>();
		}

	}

	int getNumP()
	{
		return nump;
	}

	// this class only meant to be inside VirtualMultiArray and only constructed once so, only needs to be moved only once
    VirtualArray& operator=(VirtualArray&&) = default;

	ClContext getContext(){ return *ctx; }

	~VirtualArray(){}
private:

	// gpu buffer size
	size_t sz;

	// page size
	int szp;

	// number of active pages
	int nump;

	// opencl device
	std::shared_ptr<ClDevice> dv;

	// opencl context
	std::shared_ptr<ClContext> ctx;

	// opencl queue
	std::shared_ptr<ClCommandQueue> q;

	// kernel + parameters for "find"
	std::unique_ptr<ClCompute> computeFind;

	// opencl buffer in graphics card
	std::shared_ptr<ClArray<T>> gpu;

	// opencl-pinned buffer in RAM
	std::shared_ptr<Page<T>> cpu;


	inline
	void updatePage(Page<T> * const sel, const size_t & selectedPage) const
	{
		if (sel->isEdited())
		{
			// upload edited
			cl_int err = clEnqueueWriteBuffer(q->getQueue(), gpu->getMem(), CL_FALSE, sizeof(T) * (sel->getTargetGpuPage()) * szp, sizeof(T) * szp, sel->ptr(), 0, nullptr, nullptr);
			if (CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: write buffer");
			}
			
			// download new
			sel->setTargetGpuPage(selectedPage);
			err = clEnqueueReadBuffer(q->getQueue(), gpu->getMem(), CL_FALSE, sizeof(T) * selectedPage * szp, sizeof(T) * szp, sel->ptr(), 0, nullptr, nullptr);
			if (CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: read buffer");
			}
			
			
		}
		else
		{
			// download new
			sel->setTargetGpuPage(selectedPage);
			cl_int err = clEnqueueReadBuffer(q->getQueue(), gpu->getMem(), CL_FALSE, sizeof(T) * selectedPage * szp, sizeof(T) * szp, sel->ptr(), 0, nullptr, nullptr);
			if (CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: write buffer");
			}				
		}
		
		clFinish(q->getQueue());
	}
};



#endif /* VIRTUALARRAY_H_ */
