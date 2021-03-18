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
class Cache
{
public:
	Cache():size(0),ctr(0),szp(0),gpu(nullptr),q(nullptr){ ctrEvict=0; cacheHit=0; cacheMiss=0;  fImplementation= [&](const size_t & ind){ Page<T> * result=nullptr; return result;};}


	Cache(size_t sizePrm, std::shared_ptr<ClCommandQueue> cq, std::shared_ptr<ClArray<T>> arr,
			int pageSize, bool usePinnedArraysOnly,
			std::shared_ptr<Page<T>> cpuArr,
			bool hitRatioDebuggingEnabled=false):size(sizePrm),ctr(0),szp(pageSize)
	{
		cacheHit=0;
		cacheMiss=0;
		q=cq;
		gpu=arr;
		ctr=0;
		ctrEvict=size/2;

		for(int i=0;i<sizePrm;i++)
		{
			Page<T> * page = cpuArr.get()+i;
			updatePage(page, (size_t)i);
			page->reset();

			usagePagePtr.push_back(page);
			usageIndex.push_back((size_t)i);
			usageUsed.push_back(0);
			fastMapping[(size_t)i]=i;
		}

		if(hitRatioDebuggingEnabled)
		{
			fImplementation = [&](const size_t & index ){ return accessClock2HandDebug(index);};
		}
		else
		{
			fImplementation = [&](const size_t & index ){ return accessClock2Hand(index);};
		}
	}


	Page<T> * const access(const size_t & index)
	{
		return fImplementation(index);
	}

	// CLOCK algorithm with 2 hand counters
	Page<T> * const accessClock2Hand(const size_t & index)
	{

		typename std::unordered_map<size_t,unsigned int>::iterator it = fastMapping.find(index);
		if(it!=fastMapping.end())
		{
			usageUsed[it->second]=1;
			return usagePagePtr[it->second];
		}
		else
		{
			int ctrFound = -1;
			while(ctrFound==-1)
			{
				if(usageUsed[ctr]>0)
				{
					usageUsed[ctr]=0;
				}

				ctr++;
				if(ctr>=size)
				{
					ctr=0;
				}

				if(usageUsed[ctrEvict]==0)
				{
					ctrFound=ctrEvict;
				}

				ctrEvict++;
				if(ctrEvict>=size)
				{
					ctrEvict=0;
				}
			}

			if(usagePagePtr[ctrFound]->getTargetGpuPage()!=index)
			{
				updatePage(usagePagePtr[ctrFound], index);
				usagePagePtr[ctrFound]->reset();
			}

			fastMapping.erase(usageIndex[ctrFound]);
			fastMapping[index]=ctrFound;
			usageIndex[ctrFound]=index;

			return usagePagePtr[ctrFound];

		}
	}


	Page<T> * const accessClock2HandDebug(const size_t & index)
	{

		typename std::unordered_map<size_t,unsigned int>::iterator it = fastMapping.find(index);
		if(it!=fastMapping.end())
		{
			usageUsed[it->second]=1;
			cacheHit++;
			return usagePagePtr[it->second];
		}
		else
		{
			int ctrFound = -1;
			while(ctrFound==-1)
			{
				if(usageUsed[ctr]>0)
				{
					usageUsed[ctr]=0;
				}

				ctr++;
				if(ctr>=size)
				{
					ctr=0;
				}

				if(usageUsed[ctrEvict]==0)
				{
					ctrFound=ctrEvict;
				}

				ctrEvict++;
				if(ctrEvict>=size)
				{
					ctrEvict=0;
				}
			}

			if(usagePagePtr[ctrFound]->getTargetGpuPage()!=index)
			{
				updatePage(usagePagePtr[ctrFound], index);
				usagePagePtr[ctrFound]->reset();
			}

			fastMapping.erase(usageIndex[ctrFound]);
			fastMapping[index]=ctrFound;
			usageIndex[ctrFound]=index;
			cacheMiss++;
			return usagePagePtr[ctrFound];

		}
	}


	void resetCacheHit() noexcept
	{
		cacheHit=0;
	}

	void resetCacheMiss() noexcept
	{
		cacheMiss=0;
	}

	size_t getTotalAccess() const noexcept
	{
		return cacheMiss + cacheHit;
	}

	double getCacheHitRatio() const noexcept
	{
		return cacheHit/(double)(cacheHit+cacheMiss);
	}
private:
	size_t size;
	unsigned int ctr;
	unsigned int ctrEvict;
	std::vector<Page<T>*> usagePagePtr;
	std::vector<size_t> usageIndex;
	std::vector<unsigned int> usageUsed;
	std::unordered_map<size_t,unsigned int> fastMapping;

	std::function<Page<T>*(const size_t&)> fImplementation;

	std::shared_ptr<ClCommandQueue> q;
	std::shared_ptr<ClArray<T>> gpu;
	size_t cacheHit;
	size_t cacheMiss;
	int szp;

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
