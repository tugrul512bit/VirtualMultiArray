/*
 * GraphicsCardSupplyDepot.h
 *
 *  Created on: Feb 1, 2021
 *      Author: tugrul
 */

#ifndef GRAPHICSCARDSUPPLYDEPOT_H_
#define GRAPHICSCARDSUPPLYDEPOT_H_

#include<vector>
#include<memory>

#include"ClPlatform.h"
#include"ClDevice.h"

// prepares a list of graphics card found in system
// is not needed to be spanning VirtualMultiArray. Once  VirtualMultiArray is constructed, it contains its own resource managemennt
class GraphicsCardSupplyDepot
{
public:
	// prepares a list of all graphics cards in system.
	// debug=true: writes names and vram sizes of graphics cards on std::cout
	GraphicsCardSupplyDepot(const bool debug=false)
	{

		platformPtr = std::make_shared<ClPlatform>();

		deviceVecPtr = std::make_shared<std::vector<ClDevice>>();

		unsigned int n = platformPtr->size();
		for(unsigned int i=0;i<n;i++)
		{
			ClDevice dev(platformPtr->id(i),debug);

			auto dl = dev.generate();

			for(auto e:dl)
				deviceVecPtr->push_back(e);
		}
	}

	// gets list of already-prepared graphics cards
	// can be used for multiple VirtualMultiArray instances
	// ClDevices share same raw device-pointer under the hood
	std::vector<ClDevice> requestGpus(){ return *deviceVecPtr; }

	~GraphicsCardSupplyDepot()
	{

	}
private:
	std::shared_ptr<ClPlatform> platformPtr;
	std::shared_ptr<std::vector<ClDevice>> deviceVecPtr;
};




#endif /* GRAPHICSCARDSUPPLYDEPOT_H_ */
