/*
 * Page.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef PAGE_H_
#define PAGE_H_

#include<memory>
#include"ClCommandQueue.h"
#include"ClContext.h"
#include"AlignedCpuArray.h"

// active page that holds data in RAM by pinned buffers
template<typename T>
class Page
{
public:
	// do not use this
	Page(){arr=nullptr; edited=false;targetGpuPage=-1;}

	// allocates pinned array for a "page", uses opencl way of pinning the array and is meant to be used in its own command queue
	Page(int sz,ClContext ctxP, ClCommandQueue cqP){ arr=std::shared_ptr<AlignedCpuArray<T>>(new AlignedCpuArray<T>(*ctxP.ctxPtr(),cqP.getQueue(),sz,4096,true)); edited=false; targetGpuPage=-1;}

	// reading an element of virtual array
	// i: index of element
	T get(const int & i){ return arr->getArray()[i]; }

	// writing to an element of virtual array
	// i: index of element
	// val: element value to be written
	void edit(const int & i, const T & val){ arr->getArray()[i]=val; edited=true; }

	// checks if page is edited (if true, paging system will upload data to graphics card in case of a storage requirement of a different frozen page)
	bool isEdited(){ return edited; }

	// clears "edited" status, only used after getting fresh data from a frozen page
	void reset(){ edited=false; }

	// this changes index of frozen page (in a graphics card) is being written/read
	void setTargetGpuPage(size_t g){ targetGpuPage=g; }

	// this returns index of frozen page (in a graphics card) is being written/read
	size_t getTargetGpuPage() { return targetGpuPage; }

	// internal logic, for get/set methods of VirtualArray
	T * ptr(){ return arr->getArray(); }

	~Page(){}
private:
	std::shared_ptr<AlignedCpuArray<T>> arr;
	bool edited;// todo: should make this smart pointer too, if a different paging algorithm is going to be used
	size_t targetGpuPage; // todo: should make this smart pointer too, if a different paging algorithm is going to be used
};



#endif /* PAGE_H_ */
