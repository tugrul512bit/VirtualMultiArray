/*
 * AlignedCpuArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef ALIGNEDCPUARRAY_H_
#define ALIGNEDCPUARRAY_H_

#include<iostream>
#include<sys/mman.h>
#include"CL/cl.h"

template<typename T>
class AlignedCpuArray
{
public:
	AlignedCpuArray():size(0),pinned(false),arr(nullptr){}
	AlignedCpuArray(size_t sizeP, int alignment=4096, bool pinArray=false):size(sizeP),pinned(pinArray),arr((T *)aligned_alloc(alignment,sizeof(T)*sizeP))
	{
		// todo: optimize for pinned array
		if(pinned)
		{
			if(ENOMEM==mlock(arr,size)){ std::cout<<"Not enough pinned memory."<<std::endl; pinned = false; };
		}

	}

	T * const getArray() { return arr; }

	~AlignedCpuArray()
	{
		if(pinned)
		{
			munlock(arr,size);
		}

		if(arr!=nullptr)
			free(arr);
	}
private:
	const size_t size;
	bool pinned;
	T * const arr;

};



#endif /* ALIGNEDCPUARRAY_H_ */
