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
#include <filesystem>

// C++17
// FASTA file indexer that caches bits of data in video-memory
// supports maximum 12 physical graphics cards (with combined video memory size of them)
// supports 10 million indices
// uses RAM temporarily = (total video memory size in GB) * 80MB = 100 GB total VRAM means 8GB temporary RAM usage
class FastaGeneIndexer
{
public:
	FastaGeneIndexer(){}
	FastaGeneIndexer(std::string fileName){
		// get file size
		size_t bytes = countFileBytes(fileName);


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
		size_t nId = index.size();
		if(nId==0)
		{
			throw std::invalid_argument("Error: file without FASTA formatted gene sequence.");
		}
		std::sort(index.begin(),index.end());

		size_t ctr = 0;

		while(((ctr+index[nId-1]) < bytes) && (data.get(ctr+index[nId-1])!='\0'))
		{
			ctr++;
		}

		index.push_back(ctr+index[nId-1]);// marking end of data

	}

	// get a gene sequence at index=id
	// random access latency (with pcie v2.0 4x, fx8150-2.1GHz, 1333MHz ddr3) is 5-10 microseconds
	// thread-safe
	std::string get(size_t id)
	{
		// last element is just marker
		if(id>=index.size()-1)
			throw std::invalid_argument("Error: index out of bounds.");


		std::vector<char> dat = data.readOnlyGetN(index[id],index[id+1] - index[id]);
		dat.push_back('\0');
		return std::string(dat.data());
	}

	// get number of bytes in a gene at index id
	size_t getSize(size_t id)
	{
		return index[id+1] - index[id];
	}

	// number of indexed genes
	size_t n()
	{
		return index.size()-1;
	}

	// returns number of opencl data channels used
	int numGpuChannels()
	{
		return data.totalGpuChannels();
	}
private:

	// list of indices of each gene
	std::vector<size_t> index;

	// byte data in video memories
	VirtualMultiArray<char> data;

	// returns file size in resolution of 1024*1024*16 bytes (for the paging performance of virtual array)
	// will require to set '\0' for excessive bytes of last block
	size_t countFileBytes(std::string inFile)
	{
		size_t size = std::filesystem::file_size(inFile);
		return size + 1024*1024*16 - ( size%(1024*1024*16) );
	}

	void readFile(std::string inFile, VirtualMultiArray<char> & va, const size_t n, const size_t pageSize)
	{
		std::ifstream bigFile(inFile);
		constexpr size_t bufferSize = 1024*1024*16;
		std::vector<char> buf;buf.resize(bufferSize);
		size_t ctr = 0;

		while (bigFile)
		{
			if (ctr + bufferSize > n)
				throw std::invalid_argument("Error: array overflow.");

			// clear unused bytes
			if(ctr>n-bufferSize*3)
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
