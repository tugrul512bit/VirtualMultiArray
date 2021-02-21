/*
 * AlignedCpuArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef ALIGNEDCPUARRAY_H_
#define ALIGNEDCPUARRAY_H_

#include<iostream>
#include <stdexcept>


#include<CL/cl.h>

// storage of an active page
// pinned array is faster in data copying (Page class uses this for all active pages for performance)
// without pinning(page-lock), it still allocates with high alignment value(4096) to keep some performance
// this class meant to be used inside Page class within a smart pointer
template<typename T>
class AlignedCpuArray
{
public:
	// ctx: opencl context that belongs to a device(graphics card), used for multiple command queues
	// cq: opencl command queue that runs opencl api commands in-order by default
	// alignment is only for extra copying performance for large pages (like pinned buffers but less performance)
	// pinArray: uses OpenCL implementation to page-lock the memory area. If it doesn't work on a platform, the commented-out mlock/munlock parts can be used instead
	AlignedCpuArray(cl_context ctxP, cl_command_queue cqP,size_t sizeP, int alignment=4096, bool pinArray=false):size(sizeP)
	{
		ctx=ctxP;
		cq=cqP;
		pinned = pinArray;

		if(pinned)
		{
			// opencl pin-array
			cl_int err;
			mem=clCreateBuffer(ctx,CL_MEM_READ_WRITE|CL_MEM_ALLOC_HOST_PTR,size*sizeof(T),nullptr,&err);
			if(CL_SUCCESS!=err)
			{
				throw std::invalid_argument("error: mem alloc host ptr");
			}
			arr=(T *)clEnqueueMapBuffer(cq,mem,CL_TRUE,CL_MAP_READ|CL_MAP_WRITE,0,size*sizeof(T),0,nullptr,nullptr,&err);
			if(CL_SUCCESS!=err)
			{
				throw std::invalid_argument("error: map");
			}
		}
		else
		{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
// windows
			arr = (T *)_aligned_malloc(sizeof(T)*size,alignment);

#else
// linux
			arr = (T *)aligned_alloc(alignment,sizeof(T)*size);
#endif

		}


	}

	// returning constant pointer to type T for get/set access (currently only this scalar access is supported. For vectorization, T type needs to contain multiple data)
	T * const getArray() { return arr; }

	~AlignedCpuArray()
	{
		// todo: don't throw from destructor
		if(pinned)
		{
			// opencl unpin
			if(CL_SUCCESS!=clEnqueueUnmapMemObject(cq,mem,arr,0,nullptr,nullptr))
			{
				std::cout<<"error: mem unmap"<<std::endl;
			}

			if(CL_SUCCESS!=clReleaseMemObject(mem))
			{
				std::cout<<"error: release mem"<<std::endl;
			}
		}
		else
		{


#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
// windows
			if(arr!=nullptr)
				_aligned_free(arr);

#else
// linux
			if(arr!=nullptr)
				free(arr);
#endif

		}
	}
private:
	const size_t size;
	bool pinned;
	cl_context ctx;
	cl_command_queue cq;
	cl_mem mem;
	T * arr;
};



#endif /* ALIGNEDCPUARRAY_H_ */
