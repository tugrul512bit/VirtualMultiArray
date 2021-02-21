/*
 * ClContext.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef CLCONTEXT_H_
#define CLCONTEXT_H_

#include<iostream>
#include<memory>
#include"ClDevice.h"
#include<CL/cl.h>
#include<stdexcept>

// wrapper for opencl context that is used to hold multiple command queues per graphics card to overlap data copies
// smart pointer takes care of releasing its resources so that multiple instances can exist without breaking raii
class ClContext
{
public:
	ClContext(){ context = nullptr;}
	ClContext(ClDevice device, int index=0)
	{
		context = std::shared_ptr<cl_context>(new cl_context(),[](cl_context * ptr){ if(CL_SUCCESS!=clReleaseContext(*ptr)){ 
			std::cout<<"error: release context"<<std::endl;
		} delete ptr;});
		cl_int err;
		*context = clCreateContext(
				NULL,
				1,
				device.devPtr(index),
				NULL,
				NULL,
				&err
		);

		if(CL_SUCCESS!=err)
		{
			throw std::invalid_argument("error: context");
		}
	}

	// creates wrapper that shares raw context data
	ClContext(std::shared_ptr<cl_context> ctx){	 context = ctx; }

	// generates a new wrapper using same context (this is for generating multiple virtual cards that are on same physical card)
	std::shared_ptr<ClContext> generate()
	{
		return std::make_shared<ClContext>(context);
	}

	cl_context * ctxPtr(int index=0){ return context.get()+index; }

	~ClContext(){}
private:
	std::shared_ptr<cl_context> context;
};

#endif /* CLCONTEXT_H_ */
