
#include "bmd.h"

#include <cmath>
#include <string>
#include <algorithm>
#include <io.h>
#include <fcntl.h>
#include <fstream>  
#include "common.h"
#include <comutil.h>

#ifdef _DEBUG
#include <cassert>
#define VERIFY(condition) assert(SUCCEEDED(condition))
#else
#define VERIFY(condition) condition
#endif

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

static const BlackmagicRawResourceFormat s_resourceFormat = blackmagicRawResourceFormatBGRAU8;// blackmagicRawResourceFormatBGRAU8;
static const int s_maxJobsInFlight = 3;
static std::atomic<int> s_jobsInFlight = { 0 };

struct UserData
{
	unsigned long long frameIndex;
	bool* job_done = false;
	uint8_t* avs_framebuffer;
};




class CameraCodecCallback : public IBlackmagicRawCallback
{
	/* CameraCodecCallback is used to get the result of a "decode job" (getting one frame) from bmd sdk */
public:

	std::atomic<ULONG> m_refCount;

	CameraCodecCallback::CameraCodecCallback() {
		m_refCount = 1;
	}

	virtual ~CameraCodecCallback() = default;

	virtual void ReadComplete(IBlackmagicRawJob* readJob, HRESULT result, IBlackmagicRawFrame* frame)
	{
		UserData* userData = nullptr;
		VERIFY(readJob->GetUserData((void**)&userData));

		IBlackmagicRawJob* decodeAndProcessJob = nullptr;

		if (result == S_OK)
			VERIFY(frame->SetResourceFormat(s_resourceFormat));

		if (result == S_OK)
			result = frame->CreateJobDecodeAndProcessFrame(nullptr, nullptr, &decodeAndProcessJob);

		if (result == S_OK)
			VERIFY(decodeAndProcessJob->SetUserData(userData));

		if (result == S_OK)
			result = decodeAndProcessJob->Submit();

		if (result != S_OK)
		{
			if (decodeAndProcessJob)
				decodeAndProcessJob->Release();

			delete userData;
		}

		readJob->Release();
	}

	virtual void ProcessComplete(IBlackmagicRawJob* job, HRESULT result, IBlackmagicRawProcessedImage* img)
	{
		//get pointer to the pic
		UserData* userData = nullptr;
		VERIFY(job->GetUserData((void**)&userData));
		

		UINT32 w, h;
		img->GetWidth(&w);
		img->GetHeight(&h);

		unsigned int size = 0;
		void* imageData = nullptr;
		img->GetResource(&imageData);
		img->GetResourceSizeBytes(&size);

		//std::cout.write(reinterpret_cast<char*>(imageData), size);
		//write pointer into 

		////write debug file
		//char buffer[254]; // make sure it's big enough
		//snprintf(buffer, sizeof(buffer), "C:\\temp\\file.raw", userData->frameIndex);
		//std::ofstream filefile (buffer, std::ios::app | std::ios::binary);
		//if (filefile.is_open())
		//{
		//	filefile.write(reinterpret_cast<char*>(imageData), size);
		//	filefile.close();
		//}

		memcpy(userData->avs_framebuffer, imageData, size);

		//finally we can signal avisynth to go on
		*userData->job_done = true;

		delete userData;

		job->Release();
		--s_jobsInFlight;
	}

	virtual void DecodeComplete(IBlackmagicRawJob*, HRESULT) {}
	virtual void TrimProgress(IBlackmagicRawJob*, float) {}
	virtual void TrimComplete(IBlackmagicRawJob*, HRESULT) {}
	virtual void SidecarMetadataParseWarning(IBlackmagicRawClip*, BSTR, uint32_t, BSTR) {}
	virtual void SidecarMetadataParseError(IBlackmagicRawClip*, BSTR, uint32_t, BSTR) {}
	virtual void PreparePipelineComplete(void*, HRESULT) {}

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*)
	{
		return E_NOTIMPL;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef(void)
	{
		return ++m_refCount;
	}

	virtual ULONG STDMETHODCALLTYPE Release(void)
	{
		ULONG newRefCount = --m_refCount;
		if (newRefCount == 0)
			delete this;
		return newRefCount;
	}
};

bool* BRAWSDKProcessor::getFrameByNum(int frameNum, uint8_t * framebuffer) {
	/* frame is returned in callback processcomplete */
		
	bool* signal_done = new bool; //used by caller and by job to signal job done
	*signal_done = false;
	char buff[128] = {};

	HRESULT result;

	unsigned long long frameCount = 0;
	unsigned long long frameIndex = frameNum;

	//if (s_jobsInFlight >= s_maxJobsInFlight)
	//{
	//	std::this_thread::sleep_for(std::chrono::microseconds(100));
	//	continue;
	//}

	result = clip->GetFrameCount(&frameCount);

	IBlackmagicRawJob* jobRead = nullptr;
	
	CameraCodecCallback *callback = new CameraCodecCallback();

	result = codec->SetCallback(callback);
	if (result != S_OK)
	{
		sprintf(buff, "Failed to set IBlackmagicRawCallback!");
		throw std::runtime_error(buff);
	}
	unsigned long long findex = 0;
	result = clip->CreateJobReadFrame(frameNum, &jobRead);

	UserData* userData = nullptr;
	if (result == S_OK)
	{
		userData = new UserData();
		userData->frameIndex = frameIndex;
		userData->job_done = signal_done;
		userData->avs_framebuffer = framebuffer;
		VERIFY(jobRead->SetUserData(userData));
	}

	if (result == S_OK)
		result = jobRead->Submit();


	if (result != S_OK)
	{
		if (userData != nullptr)
			delete userData;

		if (jobRead != nullptr)
			jobRead->Release();
	}

	++s_jobsInFlight;

	frameIndex++;
	
	return signal_done;
	//return result;
}

HRESULT BRAWSDKProcessor::openFile(BSTR fileName) {
	
	HRESULT result = S_OK;
	void* context = nullptr;
	void* commandQueue = nullptr;

	char buff[128] = {};

	do
	{
		//in Avisynth environment, COM is already initialized
		/*result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (result != S_OK)
		{
			sprintf(buff, "COM init failed, HRESULT %d",result);
			throw std::runtime_error(buff);
		}*/

		/* get path of current dll (BRawsource.dll) as base for locating blackmagicapi dll*/
		TCHAR   DllPath[MAX_PATH] = { 0 };
		GetModuleFileName((HINSTANCE)&__ImageBase, DllPath, _countof(DllPath));

		_bstr_t bstr = _bstr_t(DllPath);
		std::string helperstring = bstr;
		std::string pathname = helperstring.substr(0,helperstring.find_last_of("\\") + 1);
		pathname = pathname.append("brawsource_dlls");

		BSTR libraryPath = _bstr_t(pathname.c_str());
		factory = CreateBlackmagicRawFactoryInstanceFromPath(libraryPath);
		SysFreeString(libraryPath);
		if (factory == nullptr)
		{
			sprintf(buff, "Failed to create IBlackmagicRawFactory!");
			throw std::runtime_error(buff);
		}


		result = factory->CreateCodec(&codec);
		if (result != S_OK)
		{
			sprintf(buff, "Failed to create IBlackmagicRaw!");
			throw std::runtime_error(buff);
		}
			
		result = codec->OpenClip(fileName, &clip);
		
		if (result != S_OK)
		{
			sprintf(buff, "Failed to open IBlackmagicRawClip!");
			throw std::runtime_error(buff);
		}

		//in bmd example, codec->SetCallback(&callback); is done here but we cannot do this, we create the callback instance per frame in getFrameByNum class


		//analyze clip props
		result = clip->GetFrameCount(&this->frameCount);
		result = clip->GetWidth(&this->width);
		result = clip->GetHeight(&this->height);
		result = clip->GetFrameRate(&this->framerate);

		//hackily try to get fraction from framerate float, bmd skd does not seem to provide num and den
		floatToFraction(this->framerate, this->framerate_num, this->framerate_den);

	} while (0);

	//todo: do this in desctructor?
	
	//codec->FlushJobs();

	//if (clip != nullptr)
	//	clip->Release();

	//if (codec != nullptr)
	//	codec->Release();

	//if (factory != nullptr)
	//	factory->Release();

	//CoUninitialize();

	//SysFreeString(fileName);

	return result;

}

