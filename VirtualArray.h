/*
 * VirtualArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef VIRTUALARRAY_H_
#define VIRTUALARRAY_H_

#include<memory>
#include<vector>

#include"ClPlatform.h"
#include"ClDevice.h"
#include"ClContext.h"
#include"ClCommandQueue.h"
#include"ClArray.h"
#include"Page.h"
#include"CL/cl.h"

// this is a non-threadsafe single-graphics-card using virtual array
template<typename T>
class VirtualArray
{
public:
	// don't use this
	VirtualArray():sz(0),szp(0),nump(0){}

	// for generating a physical-card based virtual array
	// takes a single virtual graphics card, size(in number of objects), page size(in number of objects), active pages (number of pages in interleaved order for caching)
	// sizeP: number of elements of array (VRAM backed)
	// device: opencl wrapper that contains only 1 graphics card
	// sizePageP: number of elements of each page (bigger pages = more RAM used)
	// numActivePageP: parameter for number of active pages (in RAM) for interleaved access caching (instead of LRU, etc) with less book-keeping overhead
	VirtualArray(const size_t sizeP,  ClDevice device, const int sizePageP=1024, const int numActivePageP=50, const bool usePinnedArraysOnly=true):sz(sizeP),szp(sizePageP),nump(numActivePageP){
		dv = std::make_shared<ClDevice>();
		*dv=device.generate()[0];
		ctx= std::make_shared<ClContext>(*dv,0);

		q= std::make_shared<ClCommandQueue>(*ctx,*dv);
		gpu= std::make_shared<ClArray<T>>(sz,*ctx);
		cpu= std::shared_ptr<Page<T>>(new Page<T>[nump],[](Page<T> * ptr){delete [] ptr;});
		for(int i=0;i<nump;i++)
		{
			cpu.get()[i]=Page<T>(szp,*ctx,*q,usePinnedArraysOnly);
		}
	}

	// for generating a virtual-card based virtual array
	// takes a single virtual graphics card, size(in number of objects), page size(in number of objects), active pages (number of pages in interleaved order for caching)
	// context: a shared context with other virtual cards (or a physical card)
	// sizeP: number of elements of array (VRAM backed)
	// device: opencl wrapper that contains only 1 graphics card
	// sizePageP: number of elements of each page (bigger pages = more RAM used)
	// numActivePageP: parameter for number of active pages (in RAM) for interleaved access caching (instead of LRU, etc) with less book-keeping overhead
	VirtualArray(const size_t sizeP, ClContext context, ClDevice device, const int sizePageP=1024, const int numActivePageP=50, const bool usePinnedArraysOnly=true):sz(sizeP),szp(sizePageP),nump(numActivePageP){

		dv = std::make_shared<ClDevice>();

		*dv=device.generate()[0];

		ctx= context.generate();

		q= std::make_shared<ClCommandQueue>(*ctx,*dv);

		gpu= std::make_shared<ClArray<T>>(sz,*ctx);

		cpu= std::shared_ptr<Page<T>>(new Page<T>[nump],[](Page<T> * ptr){delete [] ptr;});

		for(int i=0;i<nump;i++)
		{
			cpu.get()[i]=Page<T>(szp,*ctx,*q,usePinnedArraysOnly);
		}

	}

	// array access for reading an element at an index
	T get(const size_t & index)
	{
		const size_t selectedPage = index/szp;
		const int selectedActivePage = selectedPage % nump;
		auto & sel = cpu.get()[selectedActivePage];
		if(sel.getTargetGpuPage()==selectedPage)
		{
			return sel.get(index - selectedPage * szp);
		}
		else
		{

			if(sel.isEdited())
			{
				// upload edited
				cl_int err=clEnqueueWriteBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T)*(sel.getTargetGpuPage())* szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: write buffer: "<<selectedPage<<std::endl;
				}


				sel.setTargetGpuPage(selectedPage);
				err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer: "<<selectedPage<<std::endl;
				}
				// download new
				clFinish(q->getQueue());

			}
			else
			{
				sel.setTargetGpuPage(selectedPage);
				cl_int err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer: "<<selectedPage<<std::endl;
				}
				// download new
				clFinish(q->getQueue());


			}
			sel.reset();
			return sel.get(index - selectedPage * szp);
		}

	}

	// array access for writing to an element at an index
	// val: value to write to array
	void set(const size_t & index, const T & val)
	{
		const size_t selectedPage = index/szp;
		const int selectedActivePage = selectedPage % nump;
		auto & sel = cpu.get()[selectedActivePage];
		if(sel.getTargetGpuPage()==selectedPage)
		{
			sel.edit(index - selectedPage * szp, val);
		}
		else
		{
			if(sel.isEdited())
			{

				// upload edited
				cl_int err=clEnqueueWriteBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T)*(sel.getTargetGpuPage())* szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: write buffer"<<std::endl;
				}


				sel.setTargetGpuPage(selectedPage);
				err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer: "<<selectedPage<<std::endl;
				}
				// download new
				clFinish(q->getQueue());
			}
			else
			{
				sel.setTargetGpuPage(selectedPage);
				cl_int err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer: "<<selectedPage<<std::endl;
				}
				// download new
				clFinish(q->getQueue());

			}
			sel.edit(index - selectedPage * szp, val);
			sel.markAsEdited();
		}

	}



	// array access for reading multiple elements beginning at an index
	// n is guaranteed to no overflow by VirtualMultiArray's readOnlyGetN
	std::vector<T> getN(const size_t & index,int n)
	{
		std::vector<T> result;
		const size_t selectedPage = index/szp;
		const int selectedActivePage = selectedPage % nump;
		auto & sel = cpu.get()[selectedActivePage];
		if(sel.getTargetGpuPage()==selectedPage)
		{
			return sel.getN(index - selectedPage * szp,n);
		}
		else
		{

			if(sel.isEdited())
			{
				// upload edited
				cl_int err=clEnqueueWriteBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T)*(sel.getTargetGpuPage())* szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: write buffer: "<<selectedPage<<std::endl;
				}


				sel.setTargetGpuPage(selectedPage);
				err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer: "<<selectedPage<<std::endl;
				}
				// download new
				clFinish(q->getQueue());

			}
			else
			{
				sel.setTargetGpuPage(selectedPage);
				cl_int err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer: "<<selectedPage<<std::endl;
				}
				// download new
				clFinish(q->getQueue());


			}
			sel.reset();

			return sel.getN(index - selectedPage * szp,n);
		}

	}


	// array access for writing to an element at an index
	// val: value to write to array
	void setN(const size_t & index, const std::vector<T> & val, const size_t & valIndex, const size_t n)
	{
		const size_t selectedPage = index/szp;
		const int selectedActivePage = selectedPage % nump;
		auto & sel = cpu.get()[selectedActivePage];
		if(sel.getTargetGpuPage()==selectedPage)
		{
			sel.editN(index - selectedPage * szp, val,valIndex,n);
		}
		else
		{
			if(sel.isEdited())
			{

				// upload edited
				cl_int err=clEnqueueWriteBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T)*(sel.getTargetGpuPage())* szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: write buffer"<<std::endl;
				}


				sel.setTargetGpuPage(selectedPage);
				err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer: "<<selectedPage<<std::endl;
				}
				// download new
				clFinish(q->getQueue());
			}
			else
			{
				sel.setTargetGpuPage(selectedPage);
				cl_int err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * szp,sizeof(T)* szp,sel.ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer: "<<selectedPage<<std::endl;
				}
				// download new
				clFinish(q->getQueue());

			}
			sel.editN(index - selectedPage * szp, val, valIndex, n);
			sel.markAsEdited();
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

	VirtualArray(VirtualArray & copyDev):sz(copyDev.sz),szp(copyDev.szp),nump(copyDev.nump)
	{
		copyValues(this, copyDev);
	}

	VirtualArray(const VirtualArray & copyDev):sz(copyDev.sz),szp(copyDev.szp),nump(copyDev.nump)
	{
		copyValues(this, copyDev);
	}

	VirtualArray(VirtualArray && copyDev):sz(copyDev.sz),szp(copyDev.szp),nump(copyDev.nump)
	{
		copyValues(this, copyDev);
	}

	VirtualArray<T> & operator = (const VirtualArray<T> & copyDev)
	{
		copyValues(this,copyDev);
		return *this;
	}


	ClContext getContext(){ return *ctx; }

	~VirtualArray(){}
private:
	size_t sz;
	int szp;
	int nump;
	std::shared_ptr<ClDevice> dv;
	std::shared_ptr<ClContext> ctx;
	std::shared_ptr<ClCommandQueue> q;
	std::shared_ptr<ClArray<T>> gpu;
	std::shared_ptr<Page<T>> cpu;
};



#endif /* VIRTUALARRAY_H_ */
