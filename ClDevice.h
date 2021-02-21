/*
 * ClDevice.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef CLDEVICE_H_
#define CLDEVICE_H_

#include<stdexcept>
#include<string>
#include<iostream>
#include<vector>
#include<memory>
#include"ClPlatform.h"
#include<CL/cl.h>

// graphics card
//
class ClDevice
{
public:
	// don't use this
	ClDevice(){ device=nullptr; n=nullptr; vram=nullptr;}

	// this clones a virtual card out of physical card so that same card can overlap data copies through concurrent usage of both devices
	ClDevice(cl_device_id devId, int vramP=2){ device=std::make_shared<cl_device_id>(); *device=devId; n=std::make_shared<unsigned int>(); *n=1; vram=std::make_shared<int>(); *vram=vramP; }

	// holds all graphics cards found in a platform like nvidia,amd or intel
	// todo: add dedicated-vram query to separate integrated-gpus from this. intent is to save RAM, waste VRAM
	ClDevice(cl_platform_id platform, bool debug=false)
	{
		n=std::make_shared<unsigned int>();
		vram = std::shared_ptr<int>(new int[20],[&](int * ptr){ delete [] ptr; });
		device = std::shared_ptr<cl_device_id>(new cl_device_id[20],[&](cl_device_id * ptr){
			for(unsigned int i=0;i<*n;i++)
			{
				if(CL_SUCCESS!=clReleaseDevice(device.get()[i])){
					std::cout<<"error: release device:"<<std::to_string(i)<<std::endl;
				};
			}
			delete [] ptr;
		});

		if(CL_SUCCESS == clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU,20, device.get(), n.get()))
		{

			{
				for(int i=0;i<*n;i++)
				{
					cl_ulong memSize=0;
					if(CL_SUCCESS!=clGetDeviceInfo(device.get()[i],CL_DEVICE_GLOBAL_MEM_SIZE,sizeof(cl_ulong),&memSize,nullptr))
					{

							std::cout<<"error: debug mem size"<<std::endl;
							vram.get()[i]=2; // assumes 2GB by default
					}
					else
					{
						char name[2048];
						if(CL_SUCCESS==clGetDeviceInfo(device.get()[i], CL_DEVICE_NAME,  2048, (void *)name, nullptr))
						{
							if(debug)
							{
								std::string str = name;
								std::cout<<name<<" VRAM: "<<memSize<<" bytes"<<std::endl;

							}
							vram.get()[i]=memSize/1000000000;
						}
						else
						{
							if(debug)
							{
								std::cout<<"VRAM: "<<memSize<<" bytes"<<std::endl;
							}
							vram.get()[i]=2; // assumes 2GB by default (only affects data distribution ratio, doesn't stop usage of >=2GB VRAM on this card)
						}
					}
				}
			}
		}
		else
		{
			throw std::invalid_argument("error: device");
		}
	}

	// generates vector of devices each holding only 1 card information to be used in virtual array
	std::vector<ClDevice> generate()
	{
		std::vector<ClDevice> dev;
		for(unsigned int i=0;i<*n;i++)
		{
			dev.push_back(ClDevice(device.get()[i],vram.get()[i]));
		}
		return dev;
	}

	// for internal opencl logic
	cl_device_id * devPtr(int index=0){ return device.get()+index;}

	// VRAM size of card in GB
	// not used yet (todo: make default storage distribution related to this value instead of equal distribution)
	int vramSize(int index=0){ return vram.get()[index]; }


	~ClDevice(){}
private:
	std::shared_ptr<unsigned int> n;
	std::shared_ptr<int> vram;
	std::shared_ptr<cl_device_id> device;

};



#endif /* CLDEVICE_H_ */
