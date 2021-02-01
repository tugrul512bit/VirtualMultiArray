/*
 * ClPlatform.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef CLPLATFORM_H_
#define CLPLATFORM_H_

#include<memory>
#include"CL/cl.h"

class ClPlatform
{
public:
	ClPlatform()
	{
		platform = std::shared_ptr<cl_platform_id>(new cl_platform_id[5],[](cl_platform_id * ptr)
		{
			delete [] ptr;
		});
		n=std::make_shared<unsigned int>();
		if(CL_SUCCESS == clGetPlatformIDs( 5, platform.get(), n.get() ))
		{
			// std::cout<<"platform gathered"<<std::endl;
		}
		else
		{
			std::cout<<"error: platform"<<std::endl;
		}
	}

	// max 5 platforms supported
	cl_platform_id id(int index=0){ return platform.get()[index];}
	unsigned int size(){ return *n; }
private:
	std::shared_ptr<cl_platform_id> platform;
	std::shared_ptr<unsigned int> n;
};



#endif /* CLPLATFORM_H_ */
