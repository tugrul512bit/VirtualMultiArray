/*
 * ClCommandQueue.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef CLCOMMANDQUEUE_H_
#define CLCOMMANDQUEUE_H_

#include<iostream>
#include<memory>
#include"ClContext.h"
#include"ClDevice.h"
#include<CL/cl.h>
#include<stdexcept>

// wrapper for opencl command queue for simple usage in opencl operations
// default property is chosen when property parameter is null and this means in-order execution of opencl commands on same command queue
// smart pointer is taking care of releasing its resources but be cautious for scope. Every higher level object needs to stay alive until lower levels are freed
class ClCommandQueue
{
public:
	ClCommandQueue(){ q=nullptr;}
	ClCommandQueue(ClContext ctx, ClDevice dev)
	{
		q=std::shared_ptr<cl_command_queue>(new cl_command_queue(),[](cl_command_queue * ptr){ if(CL_SUCCESS!=clReleaseCommandQueue(*ptr)){std::cout<<"error: release queue"<<std::endl;} delete ptr;});
		cl_int err;
		*q=clCreateCommandQueue(
						*ctx.ctxPtr(),
						*dev.devPtr(),
						0,
						&err
		);



		if(CL_SUCCESS!=err)
		{
			throw std::invalid_argument("error: command queue");
		}
	}


	cl_command_queue getQueue(){ return *q;}

	~ClCommandQueue(){}
private:
	std::shared_ptr<cl_command_queue> q;

};



#endif /* CLCOMMANDQUEUE_H_ */
