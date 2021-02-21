/*
 * ClPlatform.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef CLPLATFORM_H_
#define CLPLATFORM_H_

#include<stdexcept>
#include<iostream>
#include<memory>

#include<CL/cl.h>


// opencl platform that belongs to a vendor like amd, intel or nvidia
class ClPlatform
{
public:
	// creates a platform to be used for generating devices->contexts -> command-queues
	ClPlatform()
	{
		platform = std::shared_ptr<cl_platform_id>(new cl_platform_id[5],[](cl_platform_id * ptr)
		{

			// opencl-c binding doesn't have release-platform
			delete [] ptr;
		});
		n=std::make_shared<unsigned int>();
		*n=0;

		if(CL_SUCCESS != clGetPlatformIDs( 5, platform.get(), n.get() ))
		{
			throw std::invalid_argument("error: platform");
		}
	}

	// max 5 platforms supported (todo: add a filter for selecting a specific platfor i.e. "non-experimental"/"nvidia"/"discrete gpu")
	cl_platform_id id(int index=0){ return platform.get()[index];}
	unsigned int size(){ return *n; }
private:
	std::shared_ptr<cl_platform_id> platform;
	std::shared_ptr<unsigned int> n;
};



#endif /* CLPLATFORM_H_ */
