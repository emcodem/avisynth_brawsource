

#ifndef BMDPROCESSORHEADER_H
#define BMDPROCESSORHEADER_H

#include "C:\Program Files (x86)\Blackmagic Design\Blackmagic RAW\Blackmagic RAW SDK\Win\Include\BlackmagicRawAPIDispatch.h"
//built by compiling BlackmagicRawAPI.idl File (as BlackmagicRawAPI.h)
#include "generated/BlackmagicRawAPI_i.c"
//BlackmagicRawAPI.h is expected to be in src/generated folder as well (or in any other include dir

/* Note that we also add the BlackmagicRawAPIDispatch.cpp file in our project from the same folder */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

class BRAWSDKProcessor {
	
public:
	
    ~BRAWSDKProcessor();
	unsigned long long frameCount = 0;
    unsigned int width;
    unsigned int height;
    float framerate;
    int framerate_num;
    int framerate_den;
    
    uint64_t audioSamples;
    uint32_t audioBitDepth;
    uint32_t channelCount;
    uint32_t sampleRate;

	HRESULT openFile(BSTR fileName, int bitmode);
    bool* getFrameByNum(int frameNum, uint8_t* framebuffer);
    void getAudioSamples(void* buf, int64_t start, int64_t count);

    IBlackmagicRaw* codec = nullptr;
    IBlackmagicRawClip* clip = nullptr;
    IBlackmagicRawFactory* factory = nullptr;
    IBlackmagicRawConfiguration* config = nullptr;

};
#endif