/*
 * FastaGeneIndexer.h
 *
 *  Created on: Feb 21, 2021
 *      Author: tugrul
 */

#ifndef FASTAGENEINDEXER_H_
#define FASTAGENEINDEXER_H_

#include "../GraphicsCardSupplyDepot.h"
#include "../VirtualMultiArray.h"
#include <fstream>
#include <algorithm>
#include <omp.h>
#include <fstream>
#include <string>

// supports maximum 12 physical graphics cards (with combined video memory size of them)
// supports 10 million indices
// uses RAM temporarily = (total video memory size in GB) * 80MB = 100 GB total VRAM means 8GB temporary RAM usage
class FastaGeneIndexer
{
public:
	FastaGeneIndexer(std::string fileName){
		// get file size
		size_t bytes = countFileBytes(fileName);
		std::cout<<bytes<<std::endl;

		// allocate virtual array
		const size_t pageSize= 1024*32;
		const int maxActivePagesPerGpu = 10;

		GraphicsCardSupplyDepot depot;
		data = VirtualMultiArray<char>(bytes, depot.requestGpus(), pageSize, maxActivePagesPerGpu, {1,1,1,1,1,1,1,1,1,1,1,1},VirtualMultiArray<char>::MemMult::UseVramRatios);

		// fill array with file data
		readFile(fileName,data,bytes,pageSize);

		// prepare index
		char test='>'; // fasta format gene start marker
		index = data.find(test, test, 10000000);
		std::sort(index.begin(),index.end());
		size_t ctr = 0;
		size_t nId = index.size();
		while(ctr+index[nId-1] < bytes && data.get(ctr+index[nId-1])!='\0')
		{
			ctr++;
		}
		index.push_back(ctr+index[nId-1]);// marking end of data
	}

	std::string get(size_t id)
	{
		// last element is just marker
		if(id>=index.size()-1)
			throw std::invalid_argument("Error: index out of bounds.");


		std::vector<char> dat = data.readOnlyGetN(index[id],index[id+1] - index[id]);
		dat.push_back('\0');
		return std::string(dat.data());
	}

	size_t getSize(size_t id)
	{
		return index[id+1] - index[id];
	}

	size_t n()
	{
		return index.size()-1;
	}

private:
	// list of indices of each gene
	std::vector<size_t> index;

	// byte data in video memories
	VirtualMultiArray<char> data;

	// returns file size in resolution of 1024*1024 bytes (for the paging performance of virtual array)
	// will require to set '\0' for excessive bytes of last block
	size_t countFileBytes(std::string inFile)
	{
		size_t result=0;
		std::ifstream bigFile(inFile);
		constexpr size_t bufferSize = 1024*1024;
		std::vector<char> buf;
		buf.resize(bufferSize);
		while (bigFile)
		{
			bigFile.read(buf.data(), bufferSize);
			result += bufferSize;
		}

		return result;
	}

	void readFile(std::string inFile, VirtualMultiArray<char> & va, const size_t n, const size_t pageSize)
	{
		std::ifstream bigFile(inFile);
		constexpr size_t bufferSize = 1024*1024;
		std::vector<char> buf;buf.resize(bufferSize);
		size_t ctr = 0;

		while (bigFile)
		{
			if (ctr + bufferSize > n)
				throw std::invalid_argument("Error: array overflow.");

			// clear unused bytes
			if(ctr<n-1024*1024*2)
			{
				for(int i=0;i<bufferSize;i++)
					buf[i]='\0';
			}
			bigFile.read(buf.data(), bufferSize);

			#pragma omp parallel for
			for(size_t i=0;i<bufferSize/pageSize;i++)
			{
				std::vector<char> tmp(buf.begin()+i*pageSize,buf.begin()+i*pageSize + pageSize);
				va.writeOnlySetN(ctr + i*pageSize,tmp);
			}

			ctr+=bufferSize;
		}
	}
};



#endif /* FASTAGENEINDEXER_H_ */
