/*
 * ClArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef CLARRAY_H_
#define CLARRAY_H_

#include<memory>
#include"CL/cl.h"

template<typename T>
class ClArray
{
public:
	ClArray(){ mem=nullptr; n=nullptr; }
	ClArray(size_t size, ClContext context)
	{
		n=std::make_shared<size_t>();
		*n=size;

		mem=std::shared_ptr<cl_mem>(new cl_mem(),[](cl_mem * ptr){if(CL_SUCCESS!=clReleaseMemObject(*ptr)){std::cout<<"error: release mem"<<std::endl;} delete ptr;});
		cl_int err;
		*mem = clCreateBuffer( *context.ctxPtr(), CL_MEM_READ_WRITE,sizeof(T) * *n,NULL,&err);
		if(CL_SUCCESS != err)
		{
			std::cout<<"error: buffer"<<std::endl;
		}
	}

	cl_mem getMem(){ return *mem; }

private:
	std::shared_ptr<cl_mem> mem;
	std::shared_ptr<size_t> n;

};



#endif /* CLARRAY_H_ */
