/*
 * ClArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef CLARRAY_H_
#define CLARRAY_H_

#include<iostream>
#include<memory>
#include<CL/cl.h>
#include<stdexcept>

// wrapper for graphics card data storage
// each instance stores all data of a virtual card. Using more virtual cards (which are generated from a common physical card) make this smaller sized.
//                                                                           (so, if only 1 card instance can not allocate all mem, use N virtual-cards)
//                                                                           (std::vector<int> parameter of VirtualMultiArray constructor {1,2,3,...})
//                                                                           (default value of this parameter is {4,4,..} which holds 25% of array data per v-card)
template<typename T>
class ClArray
{
public:
	// do not use this
	ClArray(){ mem=nullptr; n=nullptr; }

	// size: number of elements (of type T)
	// context: opencl context that belongs to a virtual card.
	// normally contexts are not good for asynchronous copies, so a physical card shares same context with all of its virtual cards to overlap data copies in multi-threaded array access
	ClArray(size_t size, ClContext context)
	{
		n=std::make_shared<size_t>();
		*n=size;

		mem=std::shared_ptr<cl_mem>(new cl_mem(),[](cl_mem * ptr){if(CL_SUCCESS!=clReleaseMemObject(*ptr)){std::cout<<"error: release mem"<<std::endl;} delete ptr;});
		cl_int err;
		*mem = clCreateBuffer( *context.ctxPtr(), CL_MEM_READ_WRITE,sizeof(T) * *n,NULL,&err);
		if(CL_SUCCESS != err)
		{
			throw std::invalid_argument("error: buffer");
		}
	}

	// this is for internal logic only that just serves memory handle for opencl operations
	cl_mem getMem(){ return *mem; }
	cl_mem * getMemPtr(){ return mem.get(); }

private:
	// these should be enough for keeping track of data in multi-threaded usage without causing any double-free errors
	std::shared_ptr<cl_mem> mem;
	std::shared_ptr<size_t> n;

};



#endif /* CLARRAY_H_ */
