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
#include"CL/cl.h"

class ClContext
{
public:
	ClContext(){ context = nullptr;}
	ClContext(ClDevice device, int index=0)
	{
		context = std::shared_ptr<cl_context>(new cl_context(),[](cl_context * ptr){ if(CL_SUCCESS!=clReleaseContext(*ptr)){ std::cout<<"error: release context"<<std::endl; } delete ptr;});
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
			std::cout<<"error: context"<<std::endl;
		}
	}

	ClContext(std::shared_ptr<cl_context> ctx){	 context = ctx; }

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
