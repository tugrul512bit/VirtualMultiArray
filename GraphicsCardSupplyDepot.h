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

class GraphicsCardSupplyDepot
{
public:
	GraphicsCardSupplyDepot()
	{
		platformPtr = std::make_shared<ClPlatform>();
		deviceVecPtr = std::make_shared<std::vector<ClDevice>>();

		unsigned int n = platformPtr->size();
		for(unsigned int i=0;i<n;i++)
		{
			ClDevice dev(platformPtr->id(i));
			auto dl = dev.generate();
			for(auto e:dl)
				deviceVecPtr->push_back(e);
		}
	}

	std::vector<ClDevice> requestGpus(){ return *deviceVecPtr; }

	~GraphicsCardSupplyDepot()
	{

	}
private:
	std::shared_ptr<ClPlatform> platformPtr;
	std::shared_ptr<std::vector<ClDevice>> deviceVecPtr;
};




#endif /* GRAPHICSCARDSUPPLYDEPOT_H_ */
