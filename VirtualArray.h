/*
 * VirtualArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef VIRTUALARRAY_H_
#define VIRTUALARRAY_H_

#include<memory>


#include"ClPlatform.h"
#include"ClDevice.h"
#include"ClContext.h"
#include"ClCommandQueue.h"
#include"ClArray.h"
#include"Page.h"
#include"CL/cl.h"

template<typename T>
class VirtualArray
{
public:
	VirtualArray(){}
	VirtualArray(size_t sizeP,  ClDevice device, int sizePageP=1024, int numActivePageP=50){
		sz = std::make_shared<size_t>();
		*sz=sizeP;
		szp= std::make_shared<int>();
		*szp=sizePageP;
		nump=std::make_shared<int>();
		*nump=numActivePageP;
		dv = std::make_shared<ClDevice>();
		*dv=device.generate()[0];
		ctx= std::make_shared<ClContext>(*dv,0);

		q= std::make_shared<ClCommandQueue>(*ctx,*dv);
		gpu= std::make_shared<ClArray<T>>(*sz,*ctx);
		cpu= std::shared_ptr<Page<T>>(new Page<T>[*nump],[](Page<T> * ptr){delete [] ptr;});
		for(int i=0;i<*nump;i++)
		{
			cpu.get()[i]=Page<T>(*szp,*ctx,*q);
		}
	}

	VirtualArray(size_t sizeP, ClContext context, ClDevice device, int sizePageP=1024, int numActivePageP=50){
		sz = std::make_shared<size_t>();
		*sz=sizeP;
		szp= std::make_shared<int>();
		*szp=sizePageP;
		nump=std::make_shared<int>();
		*nump=numActivePageP;
		dv = std::make_shared<ClDevice>();

		*dv=device.generate()[0];

		ctx= context.generate();

		q= std::make_shared<ClCommandQueue>(*ctx,*dv);

		gpu= std::make_shared<ClArray<T>>(*sz,*ctx);

		cpu= std::shared_ptr<Page<T>>(new Page<T>[*nump],[](Page<T> * ptr){delete [] ptr;});

		for(int i=0;i<*nump;i++)
		{
			cpu.get()[i]=Page<T>(*szp,*ctx,*q);
		}

	}

	T get(const size_t & index)
	{
		const size_t selectedPage = index / *szp;
		const int selectedActivePage = selectedPage % *nump;
		auto & sel = cpu.get()[selectedActivePage];
		if(sel.getTargetGpuPage()==selectedPage)
		{
			return sel.get(index - selectedPage * *szp);
		}
		else
		{
			//ctx->selectAsCurrent();
			if(sel.isEdited())
			{
				// upload edited
				cl_int err=clEnqueueWriteBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T)*(sel.getTargetGpuPage())* *szp,sizeof(T)* *szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: write buffer"<<std::endl;
				}


				sel.setTargetGpuPage(selectedPage);
				err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * *szp,sizeof(T)* *szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer"<<std::endl;
				}
				// download new
				clFinish(q->getQueue());

			}
			else
			{
				sel.setTargetGpuPage(selectedPage);
				cl_int err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * *szp,sizeof(T)* *szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer"<<std::endl;
				}
				// download new
				clFinish(q->getQueue());


			}
			sel.reset();
			return sel.get(index - selectedPage * *szp);
		}

	}

	void set(const size_t & index, const T & val)
	{
		const size_t selectedPage = index / *szp;
		const int selectedActivePage = selectedPage % *nump;
		auto & sel = cpu.get()[selectedActivePage];
		if(sel.getTargetGpuPage()==selectedPage)
		{
			sel.edit(index - selectedPage * *szp, val);
		}
		else
		{
			//ctx->selectAsCurrent();
			if(sel.isEdited())
			{

				// upload edited
				cl_int err=clEnqueueWriteBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T)*(sel.getTargetGpuPage())* *szp,sizeof(T)* *szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: write buffer"<<std::endl;
				}


				sel.setTargetGpuPage(selectedPage);
				err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * *szp,sizeof(T)* *szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer"<<std::endl;
				}
				// download new
				clFinish(q->getQueue());
			}
			else
			{
				sel.setTargetGpuPage(selectedPage);
				cl_int err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * *szp,sizeof(T)* *szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer"<<std::endl;
				}
				// download new
				clFinish(q->getQueue());

			}
			sel.edit(index - selectedPage * *szp, val);
		}

	}


	void copyValues(VirtualArray *d1,const VirtualArray & d2)
	{
		d1->sz = d2.sz;
		d1->szp = d2.szp;
		d1->nump = d2.nump;
		d1->dv = d2.dv;
		d1->ctx = d2.ctx;
		d1->q = d2.q;
		d1->gpu = d2.gpu;
		d1->cpu = d2.cpu;
	}

	VirtualArray(VirtualArray & copyDev)
	{
		copyValues(this, copyDev);
	}

	VirtualArray(const VirtualArray & copyDev)
	{
		copyValues(this, copyDev);
	}

	VirtualArray(VirtualArray && copyDev)
	{
		copyValues(this, copyDev);
	}



	VirtualArray operator=(VirtualArray copyDev)
	{
		copyValues(this, copyDev);
		return *this;
	}

	ClContext getContext(){ return *ctx; }

	~VirtualArray(){}
private:
	std::shared_ptr<size_t> sz;
	std::shared_ptr<int> szp;
	std::shared_ptr<int> nump;
	std::shared_ptr<ClDevice> dv;
	std::shared_ptr<ClContext> ctx;
	std::shared_ptr<ClCommandQueue> q;
	std::shared_ptr<ClArray<T>> gpu;
	std::shared_ptr<Page<T>> cpu;
};



#endif /* VIRTUALARRAY_H_ */
