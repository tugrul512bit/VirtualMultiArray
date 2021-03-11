/*
 * PageCache.h
 *
 *  Created on: Mar 9, 2021
 *      Author: tugrul
 */

#ifndef PAGECACHE_H_
#define PAGECACHE_H_


/* LRU implementation */
#include<vector>
#include<algorithm>
#include<unordered_map>
#include<list>
#include<functional>
#include<memory>
#include"Page.h"


template<typename T>
struct CacheNode
{
	size_t index;
	size_t used;
	Page<T> * page;
	CacheNode():index(0),used(0),page(nullptr){}
	CacheNode(size_t i, size_t u, Page<T> * ptr):index(i),used(u),page(ptr){}
};

template<typename T>
void insertionSort(CacheNode<T>* buf, int size) {
	CacheNode<T> keyUsed;
	int j;
	for(int i = 1; i<size; i++) {
	  keyUsed = buf[i];
	  j = i;
	  while(j > 0 && buf[j-1].used>keyUsed.used) {
		  buf[j] = buf[j-1];
		 j--;
	  }
	  buf[j] = keyUsed;
	}
}

template<typename T>
class Cache
{
public:
	Cache():size(0),ctr(0),szp(0),gpu(nullptr),q(nullptr),directCache(nullptr){fImplementation= [&](const size_t & ind){ Page<T> * result=nullptr; return result;};}


	Cache(size_t sizePrm, std::shared_ptr<ClCommandQueue> cq, std::shared_ptr<ClArray<T>> arr,
			int pageSize, bool usePinnedArraysOnly, std::shared_ptr<ClContext> ctx,
			std::shared_ptr<Page<T>> cpuArr):size(sizePrm),ctr(0),szp(pageSize),directCache(nullptr)
	{
		q=cq;
		gpu=arr;

		if(sizePrm<2)
		{
			directCache = cpuArr.get();
			updatePage(directCache,0ull);
			directCache->reset();
			fImplementation=[&](const size_t & ind)
			{
				if(directCache->getTargetGpuPage()!=ind)
				{
					updatePage(directCache, ind);
					directCache->reset();
				}
				return directCache;
			};
		}
		else if(sizePrm<13)
		{
			for(int i=0;i<sizePrm;i++)
			{
				Page<T> * page = cpuArr.get()+i;
				updatePage(page, (size_t)i);
				page->reset();
				CacheNode<T> node(i,0,page);
				usage.push_back(node);
			}
			// array-based low-constant cost
			fImplementation=[&](const size_t & ind)
			{
				return accessFast(ind);
			};
		}
		else
		{
			for(int i=0;i<sizePrm;i++)
			{
				Page<T> * page = cpuArr.get()+i;
				updatePage(page, (size_t)i);
				page->reset();
				scalableCounts.push_front(page);
				scalableMapping[(size_t)i]=scalableCounts.begin();
			}
			// map-linked-list-based good scaling
			fImplementation=[&](const size_t & ind)
			{
				return accessScalable(ind);
			};
		}
	}


	Page<T> * const access(const size_t & index)
	{
		return fImplementation(index);
	}


	Page<T> * const accessScalable(const size_t & index)
	{
		Page<T> * result=nullptr;
		typename std::unordered_map<size_t,typename std::list<Page<T>*>::iterator>::iterator it = scalableMapping.find(index);
		if(it == scalableMapping.end())
		{
			// not found in cache
			Page<T> * old = scalableCounts.back();

			size_t oldIndex = old->getTargetGpuPage();
			if(old->getTargetGpuPage()!=index)
			{
				updatePage(old, index);
				old->reset();
			}

			scalableCounts.pop_back();
			scalableMapping.erase(oldIndex);


			// add a new
			scalableCounts.push_front(old);
			typename std::list<Page<T>*>::iterator iter = scalableCounts.begin();
			scalableMapping[index]=iter;

			result = old;
		}
		else
		{
			// found in cache
			// remove
			Page<T> * old = *(it->second);
			scalableCounts.erase(it->second);


			// add a new
			scalableCounts.push_front(old);
			auto iter = scalableCounts.begin();
			scalableMapping[index]=iter;

			result = old;
		}


		return result;
	}


	Page<T> * const accessFast(const size_t & index)
	{
		Page<T> * result=nullptr;
		auto it = std::find_if(usage.begin(),usage.end(),[index](const CacheNode<T>& n){ return n.index == index; });
		if(it == usage.end())
		{
			if(usage[0].page->getTargetGpuPage()!=index)
			{
				updatePage(usage[0].page, index);
				usage[0].page->reset();
			}
			usage[0].index=index;
			usage[0].used=ctr++;
			result = usage[0].page;
		}
		else
		{
			it->used=ctr++;
			result = it->page;
		}
		insertionSort(usage.data(),usage.size());

		return result;
	}
private:
	size_t size;
	size_t ctr;
	std::vector<CacheNode<T>> usage;
	std::function<Page<T>*(const size_t&)> fImplementation;
	std::unordered_map<size_t,typename std::list<Page<T>*>::iterator> scalableMapping;
	std::list<Page<T>*> scalableCounts;

	std::shared_ptr<ClCommandQueue> q;
	std::shared_ptr<ClArray<T>> gpu;
	int szp;
	Page<T> * directCache;


	inline
	void updatePage(Page<T> * const sel, const size_t & selectedPage) const
	{
		cl_event evt;
		if (sel->isEdited())
		{
			// upload edited
			cl_int err = clEnqueueWriteBuffer(q->getQueue(), gpu->getMem(), CL_FALSE, sizeof(T) * (sel->getTargetGpuPage()) * szp, sizeof(T) * szp, sel->ptr(), 0, nullptr, nullptr);
			if (CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: write buffer");
			}

			// download new
			sel->setTargetGpuPage(selectedPage);
			err = clEnqueueReadBuffer(q->getQueue(), gpu->getMem(), CL_FALSE, sizeof(T) * selectedPage * szp, sizeof(T) * szp, sel->ptr(), 0, nullptr, &evt/*nullptr*/);
			if (CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: read buffer");
			}


		}
		else
		{
			// download new
			sel->setTargetGpuPage(selectedPage);
			cl_int err = clEnqueueReadBuffer(q->getQueue(), gpu->getMem(), CL_FALSE, sizeof(T) * selectedPage * szp, sizeof(T) * szp, sel->ptr(), 0, nullptr, &evt/*nullptr*/);
			if (CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: read buffer");
			}
		}

		clFlush(q->getQueue());

		const cl_event_info evtInf = CL_EVENT_COMMAND_EXECUTION_STATUS;
		cl_int evtStatus0 = 0;
		if(CL_SUCCESS != clGetEventInfo(evt, evtInf,sizeof(cl_int), &evtStatus0, nullptr))
		{
			throw std::invalid_argument("error: event info");
		}

		while (evtStatus0 != CL_COMPLETE)
		{
			if(CL_SUCCESS != clGetEventInfo(evt, evtInf,sizeof(cl_int), &evtStatus0, nullptr))
			{
				throw std::invalid_argument("error: event info");
			}
			std::this_thread::yield();
		}
		if(CL_SUCCESS != clReleaseEvent(evt))
		{
			std::cout<<"error: release event"<<std::endl;
		}
		//clFinish(q->getQueue());
	}

};



#endif /* PAGECACHE_H_ */
