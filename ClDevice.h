/*
 * ClDevice.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef CLDEVICE_H_
#define CLDEVICE_H_

#include<iostream>
#include<vector>
#include<memory>
#include"ClPlatform.h"
#include"CL/cl.h"

class ClDevice
{
public:
	ClDevice(){ device=nullptr; n=nullptr; }
	ClDevice(cl_device_id devId){ device=std::make_shared<cl_device_id>(); *device=devId; n=std::make_shared<unsigned int>(); *n=1; }
	ClDevice(cl_platform_id platform)
	{
		device = std::shared_ptr<cl_device_id>(new cl_device_id[20],[&](cl_device_id * ptr){
			for(unsigned int i=0;i<*n;i++)
			{
				if(CL_SUCCESS!=clReleaseDevice(device.get()[i])){std::cout<<"error: release device:"<<i<<std::endl;};
			}
			delete [] ptr;
		});
		n=std::make_shared<unsigned int>();
		if(CL_SUCCESS == clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU,20, device.get(), n.get()))
		{

		}
		else
		{
			std::cout<<"error: device"<<std::endl;
		}
	}

	std::vector<ClDevice> generate()
	{
		std::vector<ClDevice> dev;
		for(unsigned int i=0;i<*n;i++)
		{
			dev.push_back(ClDevice(device.get()[i]));
		}
		return dev;
	}

	cl_device_id * devPtr(int index=0){ return device.get()+index;}

	~ClDevice(){}
private:
	std::shared_ptr<cl_device_id> device;
	std::shared_ptr<unsigned int> n;
};



#endif /* CLDEVICE_H_ */
