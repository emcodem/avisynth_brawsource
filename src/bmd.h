

#ifndef BMDPROCESSORHEADER_H
#define BMDPROCESSORHEADER_H

#include "C:\Program Files (x86)\Blackmagic Design\Blackmagic RAW\Blackmagic RAW SDK\Win\Include\BlackmagicRawAPIDispatch.h"
/* Note that we also add the BlackmagicRawAPIDispatch.cpp file in our project from the same folder */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>


class BRAWSDKProcessor {
	
public:
	

	unsigned long long frameCount = 0;
    unsigned int width;
    unsigned int height;
    float framerate;
    int framerate_num;
    int framerate_den;
	HRESULT openFile(BSTR fileName, int bitmode);
    bool* getFrameByNum(int frameNum, uint8_t* framebuffer);


    IBlackmagicRaw* codec = nullptr;
    IBlackmagicRawClip* clip = nullptr;
    IBlackmagicRawFactory* factory = nullptr;
    //IBlackmagicRaw* codec = nullptr;
    //IBlackmagicRawClip* clip = nullptr;
    IBlackmagicRawConfiguration* config = nullptr;
    //"[file]s"
    //    "[width]i"
    //    "[height]i"
    //    "[pixel_type]s"
    //    "[fpsnum]i"
    //    "[fpsden]i"
    //    "[index]s"

    //    "[show]b";

};
#endif