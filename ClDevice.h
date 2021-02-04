/*
 * ClDevice.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef CLDEVICE_H_
#define CLDEVICE_H_

#include<string>
#include<iostream>
#include<vector>
#include<memory>
#include"ClPlatform.h"
#include"CL/cl.h"

class ClDevice
{
public:
	ClDevice(){ device=nullptr; n=nullptr; vram=nullptr;}
	ClDevice(cl_device_id devId, int vramP=2){ device=std::make_shared<cl_device_id>(); *device=devId; n=std::make_shared<unsigned int>(); *n=1; vram=std::make_shared<int>(); *vram=vramP; }
	ClDevice(cl_platform_id platform, bool debug=false)
	{
		vram = std::shared_ptr<int>(new int[20],[&](int * ptr){ delete [] ptr; });
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

			{
				for(int i=0;i<*n;i++)
				{
					cl_ulong memSize=0;
					if(CL_SUCCESS!=clGetDeviceInfo(device.get()[i],CL_DEVICE_GLOBAL_MEM_SIZE,sizeof(cl_ulong),&memSize,nullptr))
					{

							std::cout<<"error: debug mem size"<<std::endl;
							vram.get()[i]=2;
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
							vram.get()[i]=2;
						}
					}
				}
			}
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
			dev.push_back(ClDevice(device.get()[i],vram.get()[i]));
		}
		return dev;
	}

	cl_device_id * devPtr(int index=0){ return device.get()+index;}

	// in GB
	int vramSize(int index=0){ return vram.get()[index]; }


	~ClDevice(){}
private:
	std::shared_ptr<int> vram;
	std::shared_ptr<cl_device_id> device;
	std::shared_ptr<unsigned int> n;
};



#endif /* CLDEVICE_H_ */
