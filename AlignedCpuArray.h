/*
 * AlignedCpuArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef ALIGNEDCPUARRAY_H_
#define ALIGNEDCPUARRAY_H_

#include<iostream>

#include"ClCommandQueue.h"
#include"ClContext.h"
#include"CL/cl.h"

template<typename T>
class AlignedCpuArray
{
public:
	AlignedCpuArray():size(0){ pinned=false; arr=nullptr;}
	AlignedCpuArray(std::shared_ptr<ClContext> ctx, std::shared_ptr<ClCommandQueue> q,size_t sizeP, int alignment=4096, bool pinArray=false):size(sizeP)
	{
		cq=q;
		cntxt=ctx;
		pinned = pinArray;
		// todo: optimize for pinned array
		if(pinned)
		{
			cl_int err;
			mem = clCreateBuffer(*(cntxt->ctxPtr()),CL_MEM_ALLOC_HOST_PTR,size,nullptr,&err);
			if(CL_SUCCESS!=err)
			{
				std::cout<<"error: host buffer"<<std::endl;
			}
			arr = (T *) clEnqueueMapBuffer(cq->getQueue(),mem,CL_TRUE,CL_MAP_READ | CL_MAP_WRITE,0,size,0,nullptr,nullptr,&err);
			if(CL_SUCCESS!=err)
			{
				std::cout<<"error: clmap"<<std::endl;
			}

		}
		else
		{
			arr = (T *)aligned_alloc(alignment,sizeof(T)*size);

		}

	}

	T * const getArray() { return arr; }

	~AlignedCpuArray()
	{
		if(pinned)
		{

			if(CL_SUCCESS!=clEnqueueUnmapMemObject(cq->getQueue(),mem,arr,0,nullptr,nullptr))
			{
				std::cout<<"error: clunmap"<<std::endl;
			}

			if(CL_SUCCESS!=clReleaseMemObject(mem))
			{
				std::cout<<"error: clunmap release"<<std::endl;
			}

		}
		else
		{
			if(arr!=nullptr)
				free(arr);
		}
	}
private:
	const size_t size;
	bool pinned;
	T * arr;
	cl_mem mem;
	std::shared_ptr<ClContext> cntxt;
	std::shared_ptr<ClCommandQueue> cq;
};



#endif /* ALIGNEDCPUARRAY_H_ */
