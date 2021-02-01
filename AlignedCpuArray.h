/*
 * AlignedCpuArray.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef ALIGNEDCPUARRAY_H_
#define ALIGNEDCPUARRAY_H_


#include"CL/cl.h"

template<typename T>
class AlignedCpuArray
{
public:
	AlignedCpuArray(){ arr=nullptr; pinned=false;}
	AlignedCpuArray(size_t size, int alignment=4096, bool pinArray=false)
	{
		// todo: optimize for pinned array
		pinned=pinArray; arr=(T *)aligned_alloc(alignment,sizeof(T)*size);
	}

	T * getArray() { return arr; }

	~AlignedCpuArray()
	{
		if(arr!=nullptr)
			free(arr);
	}
private:
	T * arr;
	bool pinned;
};



#endif /* ALIGNEDCPUARRAY_H_ */
