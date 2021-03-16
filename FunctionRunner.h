/*
 * FunctionRunner.h
 *
 *  Created on: Mar 16, 2021
 *      Author: tugrul
 */

#ifndef FUNCTIONRUNNER_H_
#define FUNCTIONRUNNER_H_

#include<mutex>
#include<queue>
#include<thread>
#include<condition_variable>
#include<memory>


template<typename T>
class Prefetcher
{
public:
	Prefetcher(){  }
	Prefetcher(Prefetcher & p) = default;
	Prefetcher(T vaPrm)
	{
		va=vaPrm;
		thr=std::thread([&](){
			size_t currentIndex;
			bool work = true;
			while(work)
			{
				{
					std::unique_lock<std::mutex> lck(mut);


					if(!iQ.empty())
					{
						currentIndex = iQ.front();
						iQ.pop();
					}
					else
					{
						cond.wait_for(lck,std::chrono::milliseconds(100));
						continue;
					}
				}

				if(currentIndex==-1)
				{
					work=false;
				}
				else
				{
					va.get(currentIndex);
				}
			}

			{
				std::unique_lock<std::mutex> lck(mut);
				while(!iQ.empty())
				{
					iQ.pop();
				}
				iQ.push(-5);
			}

		});
	}


	void push(const size_t index)
	{
		{
			std::unique_lock<std::mutex> lck(mut);
			iQ.push(index);
		}
		cond.notify_one();
	}

	~Prefetcher()
	{
		push(-1);
		bool work = true;

		while(work)
		{
			{
				std::unique_lock<std::mutex> lck(mut);
				if(!iQ.empty())
				{
					size_t index = iQ.front();
					iQ.pop();
					if(index == -5)
					{
						// close signal received
						work=false;
					}
					else if (index == -1)
					{
						iQ.push(-1);

					}
				}
			}
			cond.notify_one();
			std::this_thread::yield();
		}

		if(thr.joinable())
			thr.join();


	}
private:
	std::condition_variable cond;
	std::thread thr;
	std::mutex mut;
	std::queue<size_t> iQ;
	T va;
};


#endif /* FUNCTIONRUNNER_H_ */
