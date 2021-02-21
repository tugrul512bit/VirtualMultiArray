/*
 * ClCompute.h
 *
 *  Created on: Feb 14, 2021
 *      Author: tugrul
 */

#ifndef CLCOMPUTE_H_
#define CLCOMPUTE_H_

#include<memory>
#include<map>
#include<string>
#include <CL/cl.h>
#include "ClDevice.h"
#include "ClContext.h"
#include "ClCommandQueue.h"
#include <stdexcept>

class ClComputeParameter
{
public:
	ClComputeParameter(){mem=nullptr; built=false; name="", argIndex=-1, sizeBytes=0;}

	ClComputeParameter(ClContext ctx,std::string nameP, size_t sizeBytesP, int argIndexP, cl_mem * outerGpuBuf=nullptr)
	{
		name = nameP;
		sizeBytes=sizeBytesP;
		argIndex=argIndexP;
		cl_int err;
		mem = std::unique_ptr<cl_mem>(new cl_mem());
		if(outerGpuBuf==nullptr)
		{
			*mem = clCreateBuffer( *ctx.ctxPtr(), CL_MEM_READ_WRITE,sizeBytes,nullptr,&err);
			if(CL_SUCCESS != err)
			{
				built = false;
				throw std::invalid_argument("Error: compute parameter create failed: ");
			}
			else
			{
				built = true;
			}
		}
		else
		{
			*mem = *outerGpuBuf;
			built=false;
		}
	}

	ClComputeParameter (const ClComputeParameter &) = delete;
	ClComputeParameter & operator = (const ClComputeParameter &) = delete;

	int getIdx(){ return argIndex;}
	int getSize(){ return sizeBytes; }
	std::string getName(){ return name; }
	cl_mem getMem(){ return *mem; }
	cl_mem * getMemPtr(){ return mem.get(); }

	~ClComputeParameter(){ if(built){ if(CL_SUCCESS!=clReleaseMemObject(*mem)){ std::cout<<"Error: release kernel parameter mem object"<<std::endl;} }}
private:
	std::unique_ptr<cl_mem> mem;
	int argIndex;
	int sizeBytes;
	std::string name;
	bool built;
};


class ClCompute
{
public:
	ClCompute(){program=nullptr; kernel=nullptr; programBuilt=false; kernelBuilt = false; }
	ClCompute(ClContext ctx, ClDevice dv, std::string clKernelCodes, std::string clKernelName)
	{
		const char * c_str = clKernelCodes.c_str();
		cl_int err;
		program = clCreateProgramWithSource(
				*ctx.ctxPtr(),
				1,
				&c_str,
				NULL,
				&err );

		if(CL_SUCCESS!=err)
		{
			throw std::invalid_argument("Error: program creation failure");
			programBuilt=false;
			kernelBuilt=false;
		}
		else
		{
			err=clBuildProgram( program, 1, dv.devPtr(), nullptr, nullptr, nullptr );
			if(CL_SUCCESS != err)
			{
				throw std::invalid_argument("Error: program compilation failure");
				programBuilt=false;
				kernelBuilt=false;
			}
			else
			{
				programBuilt=true;

				const char * name = clKernelName.c_str();
				kernel = clCreateKernel( program, name, &err );
				if(CL_SUCCESS != err)
				{
					throw std::invalid_argument("Error: kernel creation failure");
					kernelBuilt = false;
				}
				else
				{
					kernelBuilt=true;
				}
			}
		}
	}

	void addParameter(ClContext ctx, std::string name, int lengthByte, int parameterIndex, cl_mem * outerGpuBuf=nullptr)
	{
		parameters[name]=std::unique_ptr<ClComputeParameter>(new ClComputeParameter(ctx, name,lengthByte,parameterIndex,outerGpuBuf));
	}

	void setKernelArgs(int arg = -1)
	{
		cl_int err;
		for(auto const & e:parameters)
		{
			if((arg == -1) || (arg == e.second->getIdx()))
			{
				err=clSetKernelArg(	kernel, e.second->getIdx(),
									sizeof(e.second->getMem()),
									(void*)(e.second->getMemPtr() )
				);

				if(CL_SUCCESS != err)
				{
					std::string errStr = ((ClComputeParameter*)(e.second.get()))->getName();
					throw std::invalid_argument(std::string("Error: kernel arg set: ")+errStr); 
				}
			}
		}
	}

	// reads "size" bytes from value
	template<typename D>
	void setArgValueAsync(std::string name, ClCommandQueue q, const D * valuePtr)
	{
		auto it = parameters.find(name);
		if(it!=parameters.end())
		{
			cl_int err;
			err=clEnqueueWriteBuffer(	q.getQueue(),
										it->second->getMem(),
										CL_FALSE,0,
										it->second->getSize(),
										valuePtr,0,nullptr,nullptr);
			if(CL_SUCCESS != err)
			{
				throw std::invalid_argument(std::string("error: (find)write buffer: setArgValueAsync: ") + name);
			}
		}
		else
		{
			throw std::invalid_argument(std::string("Error: setArgValueAsync: ") + name);
		}
	}


	void runAsync(ClCommandQueue q, size_t numThreads, size_t numLocalThreads)
	{
		size_t numThr=numThreads;
		size_t numThrLoc=numLocalThreads;
		size_t ofs = 0;
		cl_int err;
		if(CL_SUCCESS!=(err=clEnqueueNDRangeKernel(q.getQueue(),kernel,1,&ofs,&numThr,&numThrLoc,0,nullptr,nullptr)))
		{
			throw std::invalid_argument(std::string("error: kernel run: err code=")+std::to_string(err));
		}
	}


	// writes "size" bytes to value
	template<typename D>
	void getArgValueAsync(std::string name, ClCommandQueue q, D & value)
	{
		auto it = parameters.find(name);
		if(it!=parameters.end())
		{
			cl_int err;
			err=clEnqueueReadBuffer(q.getQueue(),
									it->second->getMem(),
									CL_FALSE,0,
									it->second->getSize(),
									&value,0,nullptr,nullptr);
			if(CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: (find)write buffer: setArgValueAsync ");
			}
		}
		else
		{
			throw std::invalid_argument("Error: setArgValueAsync");
		}
	}

	void sync(ClCommandQueue q){clFinish(q.getQueue());}

	size_t getArgSizeBytes(std::string name)
	{
		auto it = parameters.find(name);
		if(it!=parameters.end())
		{
			return parameters[name]->getSize();
		}
		else
		{
			return 0;
		}
	}

	~ClCompute(){
		if(kernelBuilt)
			clReleaseKernel(kernel);
		if(programBuilt)
			clReleaseProgram(program);
	}
private:
	bool programBuilt;
	bool kernelBuilt;
	cl_program program;
	cl_kernel kernel;
	std::map<std::string,std::unique_ptr<ClComputeParameter>> parameters;
};


#endif /* CLCOMPUTE_H_ */
