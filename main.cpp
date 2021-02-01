// author: huseyin tugrul buyukisik
// Virtual Object Array that uses all graphics cards in system as storage
// to achieve seamless concurrent access to all elements
// example: 4.5GB "Particle" array distributed to 3 graphics cards, each serving 1.5GB of its VRAM
//                 RAM only used for paging system, it can be altered to use more/less of it (number of active pages parameter)
// v1.0:
//		only get() set() methods, no defragmentation, no prefetching
//		array size needs to be integer multiple of page size
//		object size can be anything (4kB object size makes random access performance much better than an average SSD)
//									(44byte object needs sequential access to be fast)

#include <iostream>
#include <vector>
#include <memory>
#include <CL/cl.h>
#include <mutex>
#include <functional>

template<typename T>
class AlignedCpuArray
{
public:
	AlignedCpuArray(){ arr=nullptr; pinned=false;}
	AlignedCpuArray(size_t size, int alignment=4096, bool pinArray=false)
	{
		// todo: optimize for pinned array
		pinned=pinArray; arr=(T *)aligned_alloc(alignment,sizeof(T)*size);
	}

	T * getArray() { return arr; }

	~AlignedCpuArray()
	{
		if(arr!=nullptr)
			free(arr);
	}
private:
	T * arr;
	bool pinned;
};

class ClPlatform
{
public:
	ClPlatform()
	{
		platform = std::shared_ptr<cl_platform_id>(new cl_platform_id[5],[](cl_platform_id * ptr)
		{
			delete [] ptr;
		});
		n=std::make_shared<unsigned int>();
		if(CL_SUCCESS == clGetPlatformIDs( 5, platform.get(), n.get() ))
		{
			// std::cout<<"platform gathered"<<std::endl;
		}
		else
		{
			std::cout<<"error: platform"<<std::endl;
		}
	}

	// max 5 platforms supported
	cl_platform_id id(int index=0){ return platform.get()[index];}
	unsigned int size(){ return *n; }
private:
	std::shared_ptr<cl_platform_id> platform;
	std::shared_ptr<unsigned int> n;
};

class ClDevice
{
public:
	ClDevice(){ device=nullptr; n=nullptr; }
	ClDevice(cl_device_id devId){ device=std::make_shared<cl_device_id>(); *device=devId; n=std::make_shared<unsigned int>(); *n=1; }
	ClDevice(cl_platform_id platform)
	{
		device = std::shared_ptr<cl_device_id>(new cl_device_id[20],[&](cl_device_id * ptr){
			for(unsigned int i=0;i<*n;i++)
			{
				if(CL_SUCCESS!=clReleaseDevice(device.get()[i])){std::cout<<"error: release device:"<<i<<std::endl;};
			}
			delete [] ptr;
		});
		n=std::make_shared<unsigned int>();
		if(CL_SUCCESS == clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU,20, device.get(), n.get()))
		{

		}
		else
		{
			std::cout<<"error: device"<<std::endl;
		}
	}

	std::vector<ClDevice> generate()
	{
		std::vector<ClDevice> dev;
		for(unsigned int i=0;i<*n;i++)
		{
			dev.push_back(ClDevice(device.get()[i]));
		}
		return dev;
	}

	cl_device_id * devPtr(int index=0){ return device.get()+index;}

	~ClDevice(){}
private:
	std::shared_ptr<cl_device_id> device;
	std::shared_ptr<unsigned int> n;
};

class ClContext
{
public:
	ClContext(){ context = nullptr;}
	ClContext(ClDevice device, int index=0)
	{
		context = std::shared_ptr<cl_context>(new cl_context(),[](cl_context * ptr){ if(CL_SUCCESS!=clReleaseContext(*ptr)){ std::cout<<"error: release context"<<std::endl; } delete ptr;});
		cl_int err;
		*context = clCreateContext(
				NULL,
				1,
				device.devPtr(index),
				NULL,
				NULL,
				&err
		);

		if(CL_SUCCESS!=err)
		{
			std::cout<<"error: context"<<std::endl;
		}
	}

	ClContext(std::shared_ptr<cl_context> ctx){	 context = ctx; }

	std::shared_ptr<ClContext> generate()
	{
		return std::make_shared<ClContext>(context);
	}

	cl_context * ctxPtr(int index=0){ return context.get()+index; }

	~ClContext(){}
private:
	std::shared_ptr<cl_context> context;
};

class ClCommandQueue
{
public:
	ClCommandQueue(){ q=nullptr;}
	ClCommandQueue(ClContext ctx, ClDevice dev)
	{
		q=std::shared_ptr<cl_command_queue>(new cl_command_queue(),[](cl_command_queue * ptr){ if(CL_SUCCESS!=clReleaseCommandQueue(*ptr)){std::cout<<"error: release queue"<<std::endl;} delete ptr;});
		cl_int err;
		*q=clCreateCommandQueueWithProperties(
						*ctx.ctxPtr(),
						*dev.devPtr(),
						nullptr,
						&err
		);



		if(CL_SUCCESS!=err)
		{
			std::cout<<"error: command queue"<<std::endl;
		}
	}


	cl_command_queue getQueue(){ return *q;}

	~ClCommandQueue(){}
private:
	std::shared_ptr<cl_command_queue> q;

};

template<typename T>
class ClArray
{
public:
	ClArray(){ mem=nullptr; n=nullptr; }
	ClArray(size_t size, ClContext context)
	{
		n=std::make_shared<size_t>();
		*n=size;

		mem=std::shared_ptr<cl_mem>(new cl_mem(),[](cl_mem * ptr){if(CL_SUCCESS!=clReleaseMemObject(*ptr)){std::cout<<"error: release mem"<<std::endl;} delete ptr;});
		cl_int err;
		*mem = clCreateBuffer( *context.ctxPtr(), CL_MEM_READ_WRITE,sizeof(T) * *n,NULL,&err);
		if(CL_SUCCESS != err)
		{
			std::cout<<"error: buffer"<<std::endl;
		}
	}

	cl_mem getMem(){ return *mem; }

private:
	std::shared_ptr<cl_mem> mem;
	std::shared_ptr<size_t> n;

};

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

template<typename T>
class VirtualArray
{
public:
	VirtualArray(){}
	VirtualArray(size_t sizeP,  ClDevice device, int sizePageP=1024, int numActivePageP=50){
		sz = std::make_shared<size_t>();
		*sz=sizeP;
		szp= std::make_shared<int>();
		*szp=sizePageP;
		nump=std::make_shared<int>();
		*nump=numActivePageP;
		dv = std::make_shared<ClDevice>();
		*dv=device.generate()[0];
		ctx= std::make_shared<ClContext>(*dv,0);

		q= std::make_shared<ClCommandQueue>(*ctx,*dv);
		gpu= std::make_shared<ClArray<T>>(*sz,*ctx);
		cpu= std::shared_ptr<Page<T>>(new Page<T>[*nump],[](Page<T> * ptr){delete [] ptr;});
		for(int i=0;i<*nump;i++)
		{
			cpu.get()[i]=Page<T>(*szp);
		}
	}

	VirtualArray(size_t sizeP, ClContext context, ClDevice device, int sizePageP=1024, int numActivePageP=50){
		sz = std::make_shared<size_t>();
		*sz=sizeP;
		szp= std::make_shared<int>();
		*szp=sizePageP;
		nump=std::make_shared<int>();
		*nump=numActivePageP;
		dv = std::make_shared<ClDevice>();

		*dv=device.generate()[0];

		ctx= context.generate();

		q= std::make_shared<ClCommandQueue>(*ctx,*dv);

		gpu= std::make_shared<ClArray<T>>(*sz,*ctx);

		cpu= std::shared_ptr<Page<T>>(new Page<T>[*nump],[](Page<T> * ptr){delete [] ptr;});

		for(int i=0;i<*nump;i++)
		{
			cpu.get()[i]=Page<T>(*szp);
		}

	}

	T get(size_t index)
	{
		size_t selectedPage = index / *szp;
		int selectedActivePage = selectedPage % *nump;
		if(cpu.get()[selectedActivePage].getTargetGpuPage()==selectedPage)
		{
			return cpu.get()[selectedActivePage].get(index - selectedPage * *szp);
		}
		else
		{
			//ctx->selectAsCurrent();
			if(cpu.get()[selectedActivePage].isEdited())
			{
				// upload edited
				cl_int err=clEnqueueWriteBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T)*(cpu.get()[selectedActivePage].getTargetGpuPage())* *szp,sizeof(T)* *szp,cpu.get()[selectedActivePage].ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: write buffer"<<std::endl;
				}


				cpu.get()[selectedActivePage].setTargetGpuPage(selectedPage);
				err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * *szp,sizeof(T)* *szp,cpu.get()[selectedActivePage].ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer"<<std::endl;
				}
				// download new
				clFinish(q->getQueue());

			}
			else
			{
				cpu.get()[selectedActivePage].setTargetGpuPage(selectedPage);
				cl_int err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * *szp,sizeof(T)* *szp,cpu.get()[selectedActivePage].ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer"<<std::endl;
				}
				// download new
				clFinish(q->getQueue());


			}
			cpu.get()[selectedActivePage].reset();
			return cpu.get()[selectedActivePage].get(index - selectedPage * *szp);
		}

	}

	void set(size_t index, T val)
	{
		size_t selectedPage = index / *szp;
		int selectedActivePage = selectedPage % *nump;
		if(cpu.get()[selectedActivePage].getTargetGpuPage()==selectedPage)
		{
			cpu.get()[selectedActivePage].edit(index - selectedPage * *szp, val);
		}
		else
		{
			//ctx->selectAsCurrent();
			if(cpu.get()[selectedActivePage].isEdited())
			{

				// upload edited
				cl_int err=clEnqueueWriteBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T)*(cpu.get()[selectedActivePage].getTargetGpuPage())* *szp,sizeof(T)* *szp,cpu.get()[selectedActivePage].ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: write buffer"<<std::endl;
				}


				cpu.get()[selectedActivePage].setTargetGpuPage(selectedPage);
				err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * *szp,sizeof(T)* *szp,cpu.get()[selectedActivePage].ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer"<<std::endl;
				}
				// download new
				clFinish(q->getQueue());
			}
			else
			{
				cpu.get()[selectedActivePage].setTargetGpuPage(selectedPage);
				cl_int err=clEnqueueReadBuffer(q->getQueue(),gpu->getMem(),CL_FALSE,sizeof(T) * selectedPage * *szp,sizeof(T)* *szp,cpu.get()[selectedActivePage].ptr(),0,nullptr,nullptr);
				if(CL_SUCCESS != err)
				{
					std::cout<<"error: read buffer"<<std::endl;
				}
				// download new
				clFinish(q->getQueue());

			}
			cpu.get()[selectedActivePage].edit(index - selectedPage * *szp, val);
		}

	}


	void copyValues(VirtualArray *d1,const VirtualArray & d2)
	{
		d1->sz = d2.sz;
		d1->szp = d2.szp;
		d1->nump = d2.nump;
		d1->dv = d2.dv;
		d1->ctx = d2.ctx;
		d1->q = d2.q;
		d1->gpu = d2.gpu;
		d1->cpu = d2.cpu;
	}

	VirtualArray(VirtualArray & copyDev)
	{
		copyValues(this, copyDev);
	}

	VirtualArray(const VirtualArray & copyDev)
	{
		copyValues(this, copyDev);
	}

	VirtualArray(VirtualArray && copyDev)
	{
		copyValues(this, copyDev);
	}



	VirtualArray operator=(VirtualArray copyDev)
	{
		copyValues(this, copyDev);
		return *this;
	}

	ClContext getContext(){ return *ctx; }

	~VirtualArray(){}
private:
	std::shared_ptr<size_t> sz;
	std::shared_ptr<int> szp;
	std::shared_ptr<int> nump;
	std::shared_ptr<ClDevice> dv;
	std::shared_ptr<ClContext> ctx;
	std::shared_ptr<ClCommandQueue> q;
	std::shared_ptr<ClArray<T>> gpu;
	std::shared_ptr<Page<T>> cpu;
};


template<typename T>
class VirtualMultiArray
{
public:

// size = integer multiple of pageSize
VirtualMultiArray(size_t size, std::vector<ClDevice> device, size_t pageSizeP=1024, int numActivePage=50){


	int nDevice = device.size()*4;
	numDevice=nDevice;
	pageSize=pageSizeP;
	va = std::shared_ptr<VirtualArray<T>>( new VirtualArray<T>[nDevice],[](VirtualArray<T> * ptr){delete [] ptr;} );
	pageLock = std::shared_ptr<std::mutex>(new std::mutex[numDevice],[](std::mutex * ptr){delete [] ptr;});

	// calc interleave offset
	std::vector<size_t> gpuPageIndex;
	for(int i=0;i<nDevice;i++)
	{
		gpuPageIndex.push_back(0);
	}

	size_t numPage = size/pageSize;
	for(size_t i=0;i<numPage;i++)
	{
		mGpuIndex.push_back(gpuPageIndex[i%nDevice]++);
	}

	for(int i=0;i<nDevice;i+=4)
	{
		va.get()[i]=VirtualArray<T>(	gpuPageIndex[i] 	* pageSize,device[i/4],pageSize,numActivePage);

		va.get()[i+1]=VirtualArray<T>(	gpuPageIndex[i+1] 	* pageSize,va.get()[i].getContext(),device[i/4],pageSize,numActivePage);

		va.get()[i+2]=VirtualArray<T>(	gpuPageIndex[i+2] 	* pageSize,va.get()[i].getContext(),device[i/4],pageSize,numActivePage);

		va.get()[i+3]=VirtualArray<T>(	gpuPageIndex[i+3] 	* pageSize,va.get()[i].getContext(),device[i/4],pageSize,numActivePage);
	}

}

	T get(size_t index){

		size_t selectedPage = index/pageSize;
		size_t selectedVirtualArray = selectedPage%numDevice;
		size_t selectedElement = mGpuIndex[selectedPage]*pageSize + (index%pageSize);
		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
		return va.get()[selectedVirtualArray].get(selectedElement);
	}

	void set(size_t index, T val){

		size_t selectedPage = index/pageSize;
		size_t selectedVirtualArray = selectedPage%numDevice;
		size_t selectedElement = mGpuIndex[selectedPage]*pageSize + (index%pageSize);
		std::unique_lock<std::mutex> lock(pageLock.get()[selectedVirtualArray]);
		va.get()[selectedVirtualArray].set(selectedElement,val);
	}
~VirtualMultiArray(){}
private:
std::shared_ptr<VirtualArray<T>> va;
std::vector<size_t> mGpuIndex;
size_t pageSize;
size_t numDevice;
std::shared_ptr<std::mutex> pageLock;
};


class Particle
{
public:
	Particle():x(0),y(0),z(0),vx(0),vy(0),vz(0),vx_old(0),vy_old(0),vz_old(0),m(0),id(0){}
	Particle(int idP):x(0),y(0),z(0),vx(0),vy(0),vz(0),vx_old(0),vy_old(0),vz_old(0),m(0),id(idP){ }
	int getId(){ return id; }
private:
	float x,y,z;
	float vx,vy,vz;
	float vx_old,vy_old,vz_old;
	float m;
	int id;
};

template<typename T>
void usingAllGraphicsCards(T & f)
{
	ClPlatform platform;
	std::vector<ClDevice> device;
	unsigned int n = platform.size();
	for(unsigned int i=0;i<n;i++)
	{
		ClDevice dev(platform.id(i));
		auto dl = dev.generate();
		for(auto e:dl)
			device.push_back(e);
	}


	// "device" vector is what VirtualMultiArray needs, contains all OpenCL-capable GPUs from all platforms in system
	// integrated-gpus will not save any RAM. Only discrete graphics cards with their own VRAMs
	// each physical graphics card is given 4 independent streams/commandQueues that can serve 4 CPU threads concurrently
	// for a system of 4 GPUs, a CPU with 16 threads is optimal
	// every virtual GPU is mapped to virtual array elements in an interleaved order
	// index=0 : gpu 1 stream 1
	// index=1 : gpu 1 stream 2
	// index=2 : gpu 1 stream 3
	// index=3 : gpu 1 stream 4
	// index=4 : gpu 2 stream 1
	// index=5 : gpu 2 stream 2
	// index=6 : gpu 2 stream 3
	// index=7 : gpu 2 stream 4
	// there is no threading involved in background. just what the developer uses
	// thread-safe to use get() and set() for any index
	// all virtual array data is distributed equally to all cards (its not VRAM size aware, simply distributes 1.5GB per card if 4.5GB data is needed on 3 cards)
	f(device);
}



// testing
#include <chrono>

int main(int argC, char ** argV)
{
	auto f = [&](std::vector<ClDevice> device){

		// n needs to be integer multiple of pageSize !!!!
		const size_t n = 1024*10000;
		const size_t pageSize=1024;
		const int maxActivePagesPerGpu = 100;

		VirtualMultiArray<Particle> test(n,device,pageSize,maxActivePagesPerGpu);


		std::chrono::milliseconds t1 =  std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::system_clock::now().time_since_epoch());
		#pragma omp parallel for
		for(int i=0;i<n;i++)
		{
			// seamless access to i index
			// create a particle with id=i and write to array
			test.set(i,Particle(i));
		}

		#pragma omp parallel for
		for(int i=0;i<n;i++)
		{
			// seamless access to i index
			// get particle id and compare to expected value
			if(test.get(i).getId()!=i)
			{
				std::cout<<"!!! error at "<<i<<std::endl;
			}
		}
		std::chrono::milliseconds t2 =  std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::system_clock::now().time_since_epoch());



		auto miliseconds = (t2.count()-t1.count());
		float throughput = (n * sizeof(Particle) * 2/* 1:set + 1:get */ / (miliseconds/1000.0))/(1000000.0);
		std::cout<<"Bandwidth achieved by using virtual array backed by all graphics cards: "<<throughput<<"MB/s"<<std::endl;
		if((throughput>178.1f) && (n==1024*10000) && (pageSize==1024) && (maxActivePagesPerGpu==100))
		{
			std::cout<<"Congrats. Your system is better than this system: GT1030 + K420 + K420 (1 channel 1333MHz ddr3 4GB RAM, FX8150 @ 2.1GHz)"<<std::endl;
			std::cout<<"Your system is "<<(   ((throughput/178.1f)-1.0f)*100.0f )<<"%  faster."<<std::endl;
			std::cout<<"Some other benchmark results from same system:"<<std::endl;

			std::cout<<"testing method                      object size   throughput    page size    cpu threads       total objects     active pages per gpu   RAM   VRAM"<<std::endl;
			std::cout<<"uniform distribution random access  44 bytes      3.1   MB/s    128 objects  8                 100k              4"<<std::endl;
			std::cout<<"uniform distribution random access  4kB           676.5 MB/s    1   object   8                 100k              4"<<std::endl;
			std::cout<<"serial access per thread            4kB           496.4 MB/s    1   object   8                 100k              4"<<std::endl;
			std::cout<<"serial access per thread            4kB           2467.0MB/s    32  objects  8                 100k              4"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      142.9 MB/s    32  objects  8                 100k              4"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      162.3 MB/s    32  objects  8                 1M                4"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      287.0 MB/s    1k  objects  8                 1M                4"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      140.8 MB/s    10k objects  8                 10M               4"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      427.1 MB/s    10k objects  8                 10M               100"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      299.9 MB/s    10k objects  8                 100M              100                    900MB 4.5GB"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      280.5 MB/s    10k objects  8                 100M              50                     600MB 4.5GB"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      249.1 MB/s    10k objects  8                 100M              25                     400MB 4.5GB"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      70.8  MB/s    100kobjects  8                 100M              8                      700MB 4.5GB"<<std::endl;
			std::cout<<"serial access per thread            44 bytes      251.1  MB/s   1k  objects  8                 100M              1000                   1GB   4.5GB"<<std::endl;
			std::cout<<"interleaved threading per object    44 bytes      236.1  MB/s   1k  objects  8                 100M              1000                   1GB   4.5GB"<<std::endl;
			std::cout<<"interleaved threading per object    44 bytes      139.5  MB/s   32  objects  8                 100M              1000                   700MB 4.5GB"<<std::endl;
			std::cout<<"interleaved threading per object    44 bytes      153.6  MB/s   32  objects  8                 100M              100                    500MB 4.5GB"<<std::endl;
			std::cout<<"interleaved threading per object    4kB           2474.0 MB/s   32  objects  8                 1M                5                      400MB 4.2GB"<<std::endl;
		}
	};

	usingAllGraphicsCards(f);



	return 0;
}
