/*
 BRawSource - Bridge between avisynth and Blackmagic SDK for decoding BRAW files

    Author: emcodem (emcodem at ffastrans dot com)

    This program is based on RawSource.dll(original author is Ernst Pech)
    for avisynth2.6x/Avisynth+.

*/


#include "C:\Program Files (x86)\Blackmagic Design\Blackmagic RAW\Blackmagic RAW SDK\Win\Include\BlackmagicRawAPIDispatch.h"
#include "bmd.h"
#include <io.h>
#include <fcntl.h>
#include <cinttypes>
#include <malloc.h>
#include "common.h"

#include <comutil.h>
#include <stdio.h>

#pragma comment(lib, "comsuppw.lib")
#pragma comment(lib, "kernel32.lib")

/*
    BMD SDK is referenced from original install directory. 
    Make sure that BlackmagicRawAPI.idl (comes with BMD SDK) is added to the root of your main project. 
    When compiling, VS should automatically extract BlackmagicAPI.h from the idl (using project settings MIDL section) 
    and copy the .h to ./src/generated folder which is added to the include directories, this way the referenced BlackmagicRawAPIDispatch.h
    can find the BlackmagicAPI.h.
*/

class BRawSource : public IClip {
    BRAWSDKProcessor* bmdproc;

    VideoInfo vi;

    void(__stdcall *writeDestFrame)(
        int fd, PVideoFrame& dst, uint8_t* buff, int* order, int count,
        ise_t* env);

public:

    BRawSource(const char* source, const int width, const int height,
              const int fpsnum, const int fpsden,
              const int framecount,BRAWSDKProcessor *_bmdproc, ise_t* env);
    PVideoFrame __stdcall GetFrame(int n, ise_t *env);

    ~BRawSource() { 
        /*_aligned_free(rawbuf);*/
    }

    bool __stdcall GetParity(int n) { return vi.image_type == VideoInfo::IT_TFF; }
    void __stdcall GetAudio(void *buf, int64_t start, int64_t count, ise_t* env) {}
    const VideoInfo& __stdcall GetVideoInfo() { return vi; }
    int __stdcall SetCacheHints(int cachehints,int frame_range) { return 0; }
};


BRawSource::BRawSource (const char *source, const int width, const int height,
                       const int fpsnum, const int fpsden,
                       const int framecount,BRAWSDKProcessor* _bmdproc, ise_t* env)
{
    this->bmdproc = _bmdproc;

    memset(&vi, 0, sizeof(VideoInfo));
    vi.width = width;
    vi.height = height;
    vi.SetFPS(fpsnum, fpsden);
    vi.SetFieldBased(false);

    int64_t header_offset = 0;
    int64_t frame_offset = 0;

    // we were not yet able to find any other pix_type compatible between bmd and avs, 
    // in bmd.cpp we force blackmagicRawResourceFormatBGRAU8 which matches BGRA
    // todo: support higher bit formats
    vi.pixel_type = VideoInfo::CS_BGR32; //matches blackmagicRawResourceFormatBGRAU8
    //if (vi.pixel_type == VideoInfo::CS_BGR32) {
    //    
    //    env->Invoke("FlipVertical", {});
    //}

    size_t framesize = vi.width * vi.height * vi.BitsPerPixel() / 8;

    vi.num_frames = framecount;

}


PVideoFrame __stdcall BRawSource::GetFrame(int n, ise_t* env)
{
    
    //create new avisynth frame
    PVideoFrame dst = env->NewVideoFrame(vi);
    
    //get write pointer for avs frame
    uint8_t* dstp = dst->GetWritePtr();

    //kick off bmd decoding job, hand over avisynth frame buffer pointer
    bool * job_done = this->bmdproc->getFrameByNum(n, dstp);

    //waits until bmd ProcessComplete.
    //todo: erro handling or timeout?
    while (!*job_done) {
    	std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    int pitch = dst->GetPitch();

    return dst;
}


AVSValue __cdecl initiate_everything(AVSValue args, void* user_data, ise_t* env)
{
    char buff[128] = {};

    try {
        validate(!args[0].Defined(), "No source specified");

        const char *source = args[0].AsString();
        BSTR bstrText = _com_util::ConvertStringToBSTR(source);
        
        //calls BMD SDK to open and analyze the file properties
        BRAWSDKProcessor* proc = new BRAWSDKProcessor();
        proc->openFile(bstrText);

        const int width = proc->width;
        const int height = proc->height;
        const int fpsnum = proc->framerate_num;
        const int fpsden = proc->framerate_den;

        return new BRawSource(source, width, height,  fpsnum, fpsden,
            proc->frameCount,proc,env);

    } catch (std::runtime_error& e) {
        env->ThrowError("BRawSource: %s", e.what());
    }
    return 0;
}


const AVS_Linkage* AVS_linkage = nullptr;


extern "C" __declspec(dllexport) const char* __stdcall
AvisynthPluginInit3(ise_t* env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;

    const char* args =
        "[file]s";/*
        "[lutpath]s" //we cand potentially support extracting embedded LUT to file
        */

    env->AddFunction("BRawSource", args, initiate_everything, nullptr);

    //todo: check if we support this or other mt modes
    if (env->FunctionExists("SetFilterMTMode")) {
        static_cast<IScriptEnvironment2*>(
            env)->SetFilterMTMode("BRawSource", MT_SERIALIZED, true);
    }

    return "BRawSource for AviSynth2.6x/Avisynth+.";
}

