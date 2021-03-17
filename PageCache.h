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
void insertionSort(CacheNode<T> * const __restrict__ buf, const int size) {
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
	Cache():size(0),ctr(0),szp(0),gpu(nullptr),q(nullptr),directCache(nullptr){ cacheHit=0; cacheMiss=0;  fImplementation= [&](const size_t & ind){ Page<T> * result=nullptr; return result;};}


	Cache(size_t sizePrm, std::shared_ptr<ClCommandQueue> cq, std::shared_ptr<ClArray<T>> arr,
			int pageSize, bool usePinnedArraysOnly,
			std::shared_ptr<Page<T>> cpuArr,
			bool hitRatioDebuggingEnabled=false):size(sizePrm),ctr(0),szp(pageSize),directCache(nullptr)
	{
		cacheHit=0;
		cacheMiss=0;
		q=cq;
		gpu=arr;

		if(sizePrm<2)
		{
			directCache = cpuArr.get();
			updatePage(directCache,0ull);
			directCache->reset();
			if(hitRatioDebuggingEnabled)
			{
				fImplementation=[&](const size_t & ind)
				{
					if(directCache->getTargetGpuPage()!=ind)
					{
						updatePage(directCache, ind);
						directCache->reset();
						cacheMiss++;
					}
					else
					{
						cacheHit++;
					}
					return directCache;
				};
			}
			else
			{
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
			if(hitRatioDebuggingEnabled)
			{
				fImplementation=[&](const size_t & ind)
				{
					return accessFastDebug(ind);
				};
			}
			else
			{
				fImplementation=[&](const size_t & ind)
				{
					return accessFast(ind);
				};
			}
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
			if(hitRatioDebuggingEnabled)
			{
				fImplementation=[&](const size_t & ind)
				{
					return accessScalableDebug(ind);
				};
			}
			else
			{
				fImplementation=[&](const size_t & ind)
				{
					return accessScalable(ind);
				};
			}
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



	Page<T> * const accessScalableDebug(const size_t & index)
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
			cacheMiss++;
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
			cacheHit++;
		}


		return result;
	}


	Page<T> * const accessFastDebug(const size_t & index)
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
			cacheMiss++;
		}
		else
		{
			it->used=ctr++;
			result = it->page;
			cacheHit++;
		}
		insertionSort(usage.data(),usage.size());

		return result;
	}

	size_t getCacheHit() const noexcept
	{
		return cacheHit;
	}

	size_t getCacheMiss() const noexcept
	{
		return cacheMiss;
	}

	size_t getTotalAccess() const noexcept
	{
		return cacheMiss + cacheHit;
	}

	double getCacheHitRatio()
	{
		return cacheHit/(double)(cacheHit+cacheMiss);
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
	size_t cacheHit;
	size_t cacheMiss;
	int szp;
	Page<T> * directCache;

	inline
	void updatePage(Page<T> * const sel, const size_t & selectedPage) const
	{

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
// windows
// clGetEventInfo lags too much in windows for gt1030
// no explicit idle-wait used

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
			err = clEnqueueReadBuffer(q->getQueue(), gpu->getMem(), CL_TRUE, sizeof(T) * selectedPage * szp, sizeof(T) * szp, sel->ptr(), 0, nullptr, nullptr);
			if (CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: read buffer");
			}


		}
		else
		{
			// download new
			sel->setTargetGpuPage(selectedPage);
			cl_int err = clEnqueueReadBuffer(q->getQueue(), gpu->getMem(), CL_TRUE, sizeof(T) * selectedPage * szp, sizeof(T) * szp, sel->ptr(), 0, nullptr, nullptr);
			if (CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: read buffer");
			}
		}

#else
// linux
// explicit idle-wait to overlap i/o with other threads by yield()
		cl_event evt;
		if (sel->isEdited())
		{
			// upload edited
			cl_int err = 	clEnqueueWriteBuffer(q->getQueue(), gpu->getMem(), CL_FALSE, sizeof(T) * (sel->getTargetGpuPage()) * szp, sizeof(T) * szp, sel->ptr(), 0, nullptr, nullptr);
			if (CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: write buffer");
			}

			// download new
			sel->setTargetGpuPage(selectedPage);
			err = 			clEnqueueReadBuffer(q->getQueue(), gpu->getMem(), CL_FALSE, sizeof(T) * selectedPage * szp, sizeof(T) * szp, sel->ptr(), 0, nullptr, &evt/*nullptr*/);
			if (CL_SUCCESS != err)
			{
				throw std::invalid_argument("error: read buffer");
			}


		}
		else
		{
			// download new
			sel->setTargetGpuPage(selectedPage);
			cl_int err = 	clEnqueueReadBuffer(q->getQueue(), gpu->getMem(), CL_FALSE, sizeof(T) * selectedPage * szp, sizeof(T) * szp, sel->ptr(), 0, nullptr, &evt/*nullptr*/);
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

#endif


	}



};



#endif /* PAGECACHE_H_ */
