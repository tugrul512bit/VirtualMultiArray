/*
 * CpuBenchmarker.h
 *
 *  Created on: Feb 21, 2021
 *      Author: tugrul
 */

#ifndef CPUBENCHMARKER_H_
#define CPUBENCHMARKER_H_

#include <chrono>
#include <string>
#include <iostream>
#include <iomanip>

// RAII type benchmarker
class CpuBenchmarker
{
public:
	CpuBenchmarker():CpuBenchmarker(0,"")
	{

	}

	CpuBenchmarker(size_t bytesToBench):CpuBenchmarker(bytesToBench,"")
	{

	}

	CpuBenchmarker(size_t bytesToBench, std::string infoExtra):t1(std::chrono::duration_cast< std::chrono::nanoseconds >(std::chrono::system_clock::now().time_since_epoch()))
	{
		bytes=bytesToBench;
		info=infoExtra;
	}

	~CpuBenchmarker()
	{
		std::chrono::nanoseconds t2 =  std::chrono::duration_cast< std::chrono::nanoseconds >(std::chrono::system_clock::now().time_since_epoch());
		size_t t = t2.count() - t1.count();
		if(info!=std::string(""))
			std::cout<<info<<": ";
		std::cout<<t<<" nanoseconds    ";
		if(bytes>0)
		{
		    std::cout << std::fixed;
		    std::cout << std::setprecision(2);
			std::cout <<   (bytes/(((double)t)/1000000000.0))/1000000.0 <<" MB/s";
		}
		std::cout<<std::endl;
	}

private:
	std::chrono::nanoseconds t1;
	size_t bytes;
	std::string info;
};

#endif /* CPUBENCHMARKER_H_ */
