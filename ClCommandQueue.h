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
#include"CL/cl.h"

class ClCommandQueue
{
public:
	ClCommandQueue(){ q=nullptr;}
	ClCommandQueue(ClContext ctx, ClDevice dev)
	{
		q=std::shared_ptr<cl_command_queue>(new cl_command_queue(),[](cl_command_queue * ptr){ if(CL_SUCCESS!=clReleaseCommandQueue(*ptr)){std::cout<<"error: release queue"<<std::endl;} delete ptr;});
		cl_int err;
		*q=clCreateCommandQueueWithProperties(
						*ctx.ctxPtr(),
						*dev.devPtr(),
						nullptr,
						&err
		);



		if(CL_SUCCESS!=err)
		{
			std::cout<<"error: command queue"<<std::endl;
		}
	}


	cl_command_queue getQueue(){ return *q;}

	~ClCommandQueue(){}
private:
	std::shared_ptr<cl_command_queue> q;

};



#endif /* CLCOMMANDQUEUE_H_ */
