/*
 BRawSource - Bridge between avisynth and Blackmagic SDK for decoding BRAW files

    Author: emcodem (emcodem at ffastrans dot com)

    This program is based on RawSource.dll(original author is Ernst Pech)
    for avisynth2.6x/Avisynth+.

*/

/*
* 
    BMD SDK is referenced from original install directory, see bmd.h
    Make sure that BlackmagicRawAPI.idl (comes with BMD SDK) is added to the root of your main project.
    When compiling, VS should automatically extract BlackmagicAPI.h from the idl (using project settings MIDL section)
    and copy the .h to ./src/generated folder which is added to the include directories, this way the referenced BlackmagicRawAPIDispatch.h
    can find the BlackmagicAPI.h.
*/

#include "bmd.h"
#include <io.h>
#include <fcntl.h>
#include <cinttypes>
#include <malloc.h>
#include "common.h"

#include <comutil.h>
#include <stdio.h>

//logging
#include<string>
#include <fstream>

#pragma comment(lib, "comsuppw.lib")
#pragma comment(lib, "kernel32.lib")

#pragma region debugging
static inline std::string getCurrentDateTime(std::string s) {
    time_t now = time(0);
    struct tm  tstruct;
    char  buf[80];
    tstruct = *localtime(&now);
    if (s == "now")
        strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
    else if (s == "date")
        strftime(buf, sizeof(buf), "%Y-%m-%d", &tstruct);
    return  std::string(buf);
};

static inline void Logger(std::string logMsg) {
    return; //disable only for code debugging
    std::string filePath = "./log_" + getCurrentDateTime("date") + ".txt";
    std::string now = getCurrentDateTime("now");
    std::ofstream ofs(filePath.c_str(), std::ios_base::out | std::ios_base::app);
    ofs << now << '\t' << logMsg << '\n';
    ofs.close();
}
#pragma endregion

#pragma region audiosource

class BRawAudioSource : public IClip {


    VideoInfo vi;

    void(__stdcall* writeDestFrame)(
        int fd, PVideoFrame& dst, uint8_t* buff, int* order, int count,
        ise_t* env);

public:

    BRawAudioSource(const char* source, int bitmode, ise_t* env);

    ~BRawAudioSource() {
    }

    bool __stdcall GetParity(int n) { return vi.image_type == VideoInfo::IT_TFF; }
    void __stdcall GetAudio(void* buf, int64_t start, int64_t count, ise_t* env);
    PVideoFrame __stdcall GetFrame(int n, ise_t* env) { return nullptr; };

    const VideoInfo& __stdcall GetVideoInfo() { return vi; }
    int __stdcall SetCacheHints(int cachehints, int frame_range) { return 0; }

    //non avisynth fields and funcs
    BRAWSDKProcessor* bmdaudioproc;
    int bitmode = 8;

};

BRawAudioSource::BRawAudioSource(const char* source, int bitmode, ise_t* env) {
    Logger("Audio Source init start");
    this->bmdaudioproc = new BRAWSDKProcessor();
    BSTR bstrText = _com_util::ConvertStringToBSTR(source);
    this->bmdaudioproc->openFile(bstrText, bitmode);
    //audio:
   
    vi.nchannels = bmdaudioproc->channelCount;
    vi.num_audio_samples = bmdaudioproc->audioSamples;
    vi.audio_samples_per_second = bmdaudioproc->sampleRate;
    
    switch (bmdaudioproc->audioBitDepth) {
        case 8:
            vi.sample_type = SAMPLE_INT8;
            break;
        case 16:
            vi.sample_type = SAMPLE_INT16;
            break;
        case 24:
            vi.sample_type = SAMPLE_INT24;
            break;
        case 32:
            vi.sample_type = SAMPLE_INT32;
            break;
        default:
            break;
    }
    vi.SetChannelMask(false, 0);

    Logger("Audio Source init done");
}

void __stdcall BRawAudioSource::GetAudio(void* buf, int64_t start, int64_t count, ise_t* env) {
    bool debughere = true;
    try {
        bmdaudioproc->getAudioSamples(buf, start, count);
    }
   catch (std::runtime_error& e) {
         env->ThrowError("BRawSource: %s", e.what());
    }
}

#pragma endregion audiosource

#pragma region videosource

class BRawSource : public IClip {
    
    VideoInfo vi;

    void(__stdcall *writeDestFrame)(
        int fd, PVideoFrame& dst, uint8_t* buff, int* order, int count,
        ise_t* env);

public:

    BRawSource(const char *source,int bitmode, ise_t* env);
    
    ~BRawSource() {}

    bool __stdcall GetParity(int n) { return vi.image_type == VideoInfo::IT_TFF; }
    void __stdcall GetAudio(void* buf, int64_t start, int64_t count, ise_t* env);
    PVideoFrame __stdcall GetFrame(int n, ise_t* env);
    const VideoInfo& __stdcall GetVideoInfo() { return vi; }
    int __stdcall SetCacheHints(int cachehints,int frame_range) { return 0; }

    //non avisynth fields and funcs
    BRAWSDKProcessor* bmdproc;
    BRawAudioSource* AudioSource;
    
    int bitmode = 8;
    PClip PostInit(ise_t* env);

};


BRawSource::BRawSource (const char *source, int bitmode, ise_t* env)
{
    Logger("BRawSource init start");
    this->bitmode = bitmode;
    this->bmdproc = new BRAWSDKProcessor();
    //const char* source = args[0].AsString();
    BSTR bstrText = _com_util::ConvertStringToBSTR(source);
    this->bmdproc->openFile(bstrText, bitmode);

    const int width = this->bmdproc->width;
    const int height = this->bmdproc->height;
    const int fpsnum = this->bmdproc->framerate_num;
    const int fpsden = this->bmdproc->framerate_den;

    memset(&vi, 0, sizeof(VideoInfo));
    vi.width = width;
    vi.height = height;
    vi.SetFPS(fpsnum, fpsden);
    vi.SetFieldBased(false);

    int64_t header_offset = 0;
    int64_t frame_offset = 0;

    // in bmd.cpp we force blackmagicRawResourceFormatXXX based on "bits" argument
    
    if (this->bitmode == 8){
        vi.pixel_type = VideoInfo::CS_BGR32; //matches blackmagicRawResourceFormatBGRAU8
    }
    if (this->bitmode == 16) {
        vi.pixel_type = VideoInfo::CS_RGBP16; //blackmagicRawResourceFormatRGBU16Planar == RGBP16, must apply  CombinePlanes( planes="RGB", source_planes="GBR",  pixel_type="RGBP16")
    }
    if (this->bitmode == 32) {
        vi.pixel_type = VideoInfo::CS_RGBAPS; //blackmagicRawResourceFormatRGBF32Planar;// == RGBAPS, must apply  CombinePlanes( planes="RGB", source_planes="GBR",  pixel_type="RGBPS")
    }
    
    size_t framesize = vi.width * vi.height * vi.BitsPerPixel() / 8;

    vi.num_frames = this->bmdproc->frameCount;

    this->AudioSource = new BRawAudioSource(source, bitmode, env);
    
    Logger("BRawSource init done");
}

PClip BRawSource::PostInit(ise_t* env) {
    //apply pix format filters mapping between bmd and avisynth, apply audio

    Logger("PostInit init start");
    PClip final_clip;
    if (this->bitmode == 8) {
        //blackmagicRawResourceFormatBGRAU8 which matches BGRA, must be flipped
        AVSValue avsv[1] = { this };
        final_clip = env->Invoke("FlipVertical", AVSValue(avsv, 1)).AsClip();
    }
    if (this->bitmode == 16) {
        // blackmagicRawResourceFormatRGBU16Planar which matches RGBP16 but B and R Planes must be switched as bmd returns BGR not RGB
        const char* names[] = { NULL, "planes", "source_planes", "pixel_type" };
        AVSValue avsv[4] = { this,"RGB","GBR","RGBP16" };
        final_clip = env->Invoke("CombinePlanes", AVSValue(avsv, 4), names).AsClip();
    }
    if (this->bitmode == 32) {
       // blackmagicRawResourceFormatRGBF32Planar which matches RGBAPS but B and R Planes must be switched as bmd returns BGR not RGB
        const char* names[] = { NULL, "planes", "source_planes", "pixel_type" };
        AVSValue avsv[4] = { this,"RGB","GBR","RGBPS"};
        final_clip = env->Invoke("CombinePlanes", AVSValue(avsv, 4),names).AsClip();
    }

    //add audio
    AVSValue ADArgs[] = { final_clip, this->AudioSource };
    PClip withAudio = env->Invoke("AudioDubEx", AVSValue(ADArgs, sizeof(ADArgs) / sizeof(ADArgs[0]))).AsClip();

    Logger("PostInit init done");
    return withAudio;
    
}

void __stdcall BRawSource::GetAudio(void* buf, int64_t start, int64_t count, ise_t* env) {
    
}

PVideoFrame __stdcall BRawSource::GetFrame(int n, ise_t* env)
{
    Logger("GetFrame start");
    //create new avisynth frame
    PVideoFrame dst = env->NewVideoFrame(vi);
    
    //get write pointer for avs frame
    uint8_t* dstp = dst->GetWritePtr();

    //kick off bmd decoding job, hand over avisynth frame buffer pointer
    bool * job_done = this->bmdproc->getFrameByNum(n, dstp);

    //waits until bmd ProcessComplete. //todo: error handling or timeout needed?
    while (!*job_done) {
    	std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    Logger("GetFrame done");
    return dst;
}

#pragma endregion videosource

#pragma region avisnyth init

AVSValue __cdecl initiate_everything(AVSValue args, void* user_data, ise_t* env)
{
    char buff[128] = {};

    try {
        validate(!args[0].Defined(), "No source specified");
        
        int bitmode;
        if (!args[1].Defined()) {
            bitmode = 8;
        }
        else {
            bitmode = args[1].AsInt();
        }
        validate(!(bitmode==8|| bitmode==16|| bitmode==32), "bit parameter must be 8,16 or 32");

        //calls BMD SDK to open and analyze the file properties
        const char* source = args[0].AsString();
        BRawSource * brawsource = new BRawSource(source, bitmode, env);
        PClip postInitClip = brawsource->PostInit(env);

        return postInitClip;

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
        "[file]s"
        "[bits]i";
        /*
        "[lutpath]s" //we cand potentially support extracting embedded LUT to file
        */

    env->AddFunction("BRawSource", args, initiate_everything, nullptr);

    return "BRawSource for AviSynth2.6x/Avisynth+.";
}

#pragma endregion avisnyth init