/*
 * AlignedCpuArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef ALIGNEDCPUARRAY_H_
#define ALIGNEDCPUARRAY_H_

#include<iostream>
#include<sys/mman.h>

#include"CL/cl.h"

template<typename T>
class AlignedCpuArray
{
public:
	AlignedCpuArray(cl_context ctxP, cl_command_queue cqP,size_t sizeP, int alignment=4096, bool pinArray=false):size(sizeP)
	{
		ctx=ctxP;
		cq=cqP;
		pinned = pinArray;
		//arr = (T *)aligned_alloc(alignment,sizeof(T)*size);
		// todo: optimize for pinned array
		if(pinned)
		{
			//if(ENOMEM==mlock(arr,size)){std::cout<<"error: mlock"<<std::endl; pinned=false; };
			cl_int err;
			mem=clCreateBuffer(ctx,CL_MEM_READ_WRITE|CL_MEM_ALLOC_HOST_PTR,size*sizeof(T),nullptr,&err);
			if(CL_SUCCESS!=err)
			{
				std::cout<<"error: mem alloc host ptr"<<std::endl;
			}
			arr=(T *)clEnqueueMapBuffer(cq,mem,CL_TRUE,CL_MAP_READ|CL_MAP_WRITE,0,size*sizeof(T),0,nullptr,nullptr,&err);
			if(CL_SUCCESS!=err)
			{
				std::cout<<"error: map"<<std::endl;
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
			//munlock(arr,size);
			if(CL_SUCCESS!=clEnqueueUnmapMemObject(cq,mem,arr,0,nullptr,nullptr))
			{
				std::cout<<"error: unmap"<<std::endl;
			}

			if(CL_SUCCESS!=clReleaseMemObject(mem))
			{
				std::cout<<"error: release mem"<<std::endl;
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
	cl_context ctx;
	cl_command_queue cq;
	cl_mem mem;
	T * arr;
};



#endif /* ALIGNEDCPUARRAY_H_ */
