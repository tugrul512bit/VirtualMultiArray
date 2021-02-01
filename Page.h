/*
 * Page.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef PAGE_H_
#define PAGE_H_

#include<memory>
#include"AlignedCpuArray.h"

template<typename T>
class Page
{
public:
	Page(){arr=nullptr; edited=false;targetGpuPage=-1;}
	Page(int sz){ arr=std::shared_ptr<AlignedCpuArray<T>>(new AlignedCpuArray<T>(sz,4096,true)); edited=false; targetGpuPage=-1;}
	T get(int i){ return arr->getArray()[i]; }
	void edit(int i, T val){ arr->getArray()[i]=val; edited=true; }
	bool isEdited(){ return edited; }
	void reset(){ edited=false; }
	void setTargetGpuPage(size_t g){ targetGpuPage=g; }
	size_t getTargetGpuPage() { return targetGpuPage; }
	T * ptr(){ return arr->getArray(); }
	~Page(){}
private:
	std::shared_ptr<AlignedCpuArray<T>> arr;
	bool edited;
	size_t targetGpuPage;
};



#endif /* PAGE_H_ */
