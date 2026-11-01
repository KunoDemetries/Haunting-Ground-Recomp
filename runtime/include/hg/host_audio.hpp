#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#ifdef _MSC_VER
#pragma comment(lib,"ole32.lib")
#endif
#endif

namespace hg {

// Host presentation only. This consumes already-modeled 48 kHz stereo PCM and
// never feeds timing or state back into the guest. Losing the host output
// device (unplugged headphones, Bluetooth sleep, default-device change) is a
// host event, not a guest fault: the device is closed, frames are dropped and
// the current default device is reopened about once per second of audio.
class HostAudioOutput {
public:
    static constexpr unsigned sample_rate=48000;
    static constexpr unsigned channels=2;
    // HG-DIAG-014 host presentation buffering. Keep enough queued PCM to absorb
    // normal Windows scheduling / preview stalls without feeding that host jitter
    // back into the guest's already-verified 48 kHz stream timing.
    static constexpr unsigned frames_per_buffer=1024;      // default chunk (21.3 ms)
    static constexpr unsigned max_frames_per_buffer=8192;
    // HG_AUDIO_BUFFER_FRAMES=256..8192 overrides the chunk size (host presentation only):
    // some virtual output devices (e.g. mixer apps) crackle with small waveOut chunks.
    static unsigned chunk_frames() {
        static const unsigned frames=[]{
            const char* v=std::getenv("HG_AUDIO_BUFFER_FRAMES");
            const unsigned n=v?unsigned(std::strtoul(v,nullptr,10)):0u;
            return n>=256 && n<=max_frames_per_buffer?n:frames_per_buffer;
        }();
        return frames;
    }
    static constexpr unsigned buffer_count=8;
    static constexpr unsigned prebuffer_count=4;
    static constexpr int gain_numerator=3;
    static constexpr int gain_denominator=4;

    bool is_open() const noexcept {
#ifdef _WIN32
        return device_ || client_;
#else
        return false;
#endif
    }
    HostAudioOutput() {
#ifdef _WIN32
        // WASAPI objects are created on the thread that feeds audio (first append), so COM
        // calls never need marshaling back to a thread that may not pump messages.
        if(use_wasapi())retry_countdown_=1;
        else if(!open_device())retry_countdown_=sample_rate; // No device yet: keep running silently and retry.
#else
        throw std::runtime_error("host speaker output is currently implemented only on Windows");
#endif
    }
    HostAudioOutput(const HostAudioOutput&)=delete;
    HostAudioOutput& operator=(const HostAudioOutput&)=delete;
    ~HostAudioOutput() noexcept {
#ifdef _WIN32
        if(!is_open())return;
        try {flush();drain();} catch(...) {}
        close_device();
#endif
    }

    void append(std::uint16_t left,std::uint16_t right) {
#ifdef _WIN32
        ++frames_;
        if(!is_open()) {
            ++dropped_frames_;
            if(retry_countdown_ && --retry_countdown_)return;
            if(!open_device()){retry_countdown_=sample_rate;return;}
            if(device_losses_)std::fprintf(stderr,"Host audio output reopened after device loss\n");
        }
        observe_queue();
        if(!wait_until_free(write_buffer_))return;
        auto& buffer=buffers_[write_buffer_];
        const auto output_left=scale_sample(left),output_right=scale_sample(right);
        observe_peak(left,input_peak_);observe_peak(right,input_peak_);
        observe_peak(output_left,output_peak_);observe_peak(output_right,output_peak_);
        buffer[write_frames_*2]=output_left;
        buffer[write_frames_*2+1]=output_right;
        ++write_frames_;
        if(write_frames_==chunk_frames())submit();
#else
        (void)left;(void)right;
#endif
    }

    void flush() {
#ifdef _WIN32
        if(!is_open())return;
        if(write_frames_) {
            auto& buffer=buffers_[write_buffer_];
            for(unsigned frame=write_frames_;frame<chunk_frames();++frame) {
                buffer[frame*2]=0;buffer[frame*2+1]=0;
            }
            submit();
        }
        if(is_open() && !started_ && submitted_buffers_)start_playback();
#endif
    }

    std::uint64_t frames() const noexcept {return frames_;}
    std::uint64_t underruns() const noexcept {return underruns_;}
    std::uint64_t dropped_frames() const noexcept {return dropped_frames_;}
    std::uint64_t device_losses() const noexcept {return device_losses_;}
    unsigned input_peak() const noexcept {return input_peak_;}
    unsigned output_peak() const noexcept {return output_peak_;}
    unsigned max_queued_buffers() const noexcept {
#ifdef _WIN32
        return max_queued_buffers_;
#else
        return 0;
#endif
    }
    bool started() const noexcept {
#ifdef _WIN32
        return started_;
#else
        return false;
#endif
    }

private:
#ifdef _WIN32
    static std::int32_t signed_sample(std::uint16_t value) {
        return value<0x8000u?std::int32_t(value):std::int32_t(value)-0x10000;
    }
    static std::uint16_t scale_sample(std::uint16_t value) {
        const auto sample=signed_sample(value);
        const auto scaled=sample*gain_numerator/gain_denominator;
        return std::uint16_t(std::int16_t(scaled));
    }
    static void observe_peak(std::uint16_t value,unsigned& peak) {
        const auto sample=signed_sample(value);
        const auto magnitude=unsigned(sample<0?-sample:sample);
        if(magnitude>peak)peak=magnitude;
    }
    static std::string error_text(const char* prefix,MMRESULT result) {
        char text[MAXERRORLENGTH]{};
        if(waveOutGetErrorTextA(result,text,MAXERRORLENGTH)==MMSYSERR_NOERROR)
            return std::string(prefix)+": "+text;
        return std::string(prefix)+" (MMRESULT "+std::to_string(result)+")";
    }
    // Opens the current default output device; false (device_ null) on failure.
    // HG_AUDIO_API=wasapi selects the WASAPI shared-mode output (Windows converts our 48 kHz
    // stereo to the device mix format); the default is waveOut. Host presentation only.
    static bool use_wasapi() {
        static const bool on=[]{const char* v=std::getenv("HG_AUDIO_API");return v && std::strcmp(v,"wasapi")==0;}();
        return on;
    }
    bool open_device() noexcept {
        if(use_wasapi())return open_wasapi();
        WAVEFORMATEX format{};
        format.wFormatTag=WAVE_FORMAT_PCM;
        format.nChannels=channels;
        format.nSamplesPerSec=sample_rate;
        format.wBitsPerSample=16;
        format.nBlockAlign=channels*sizeof(std::uint16_t);
        format.nAvgBytesPerSec=format.nSamplesPerSec*format.nBlockAlign;
        HWAVEOUT device=nullptr;
        // HG_AUDIO_DEVICE=text picks the first output whose name contains it (host only; the
        // default is the Windows default device). Names are waveOut's 31-character form.
        UINT device_id=WAVE_MAPPER;
        if(const char* want=std::getenv("HG_AUDIO_DEVICE")) {
            for(UINT id=0,count=waveOutGetNumDevs();id<count;++id) {
                WAVEOUTCAPSA caps{};
                if(waveOutGetDevCapsA(id,&caps,sizeof(caps))==MMSYSERR_NOERROR && std::strstr(caps.szPname,want)){device_id=id;break;}
            }
            if(device_id==WAVE_MAPPER)std::fprintf(stderr,"HG_AUDIO_DEVICE '%s' not found; using the default output\n",want);
            else std::fprintf(stderr,"Host audio device: %s\n",want);
        }
        if(waveOutOpen(&device,device_id,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR)return false;
        if(waveOutPause(device)!=MMSYSERR_NOERROR){waveOutClose(device);return false;}
        headers_={};
        for(unsigned n=0;n<buffer_count;++n) {
            auto& header=headers_[n];
            header.lpData=reinterpret_cast<LPSTR>(buffers_[n].data());
            header.dwBufferLength=DWORD(chunk_frames()*channels*sizeof(std::uint16_t));
            if(waveOutPrepareHeader(device,&header,sizeof(header))!=MMSYSERR_NOERROR) {
                for(unsigned prior=0;prior<n;++prior)waveOutUnprepareHeader(device,&headers_[prior],sizeof(headers_[prior]));
                waveOutClose(device);return false;
            }
        }
        device_=device;
        write_buffer_=write_frames_=submitted_buffers_=0;started_=starved_=false;
        return true;
    }
    // WASAPI shared-mode render client on the device whose friendly name contains
    // HG_AUDIO_DEVICE (else the default console device).
    bool open_wasapi() noexcept {
        const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        (void)init; // S_FALSE / RPC_E_CHANGED_MODE: COM is already usable on this thread.
        IMMDeviceEnumerator* enumerator=nullptr;IMMDevice* device=nullptr;
        if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator))))return false;
        if(const char* want=std::getenv("HG_AUDIO_DEVICE")) {
            IMMDeviceCollection* list=nullptr;UINT count=0;
            if(SUCCEEDED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&list))) {
                list->GetCount(&count);
                // PKEY_Device_FriendlyName {a45c254e-df1c-4efd-8020-67d146a850e0},14
                static const PROPERTYKEY friendly_name={{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};
                for(UINT n=0;n<count && !device;++n) {
                    IMMDevice* candidate=nullptr;IPropertyStore* store=nullptr;
                    if(FAILED(list->Item(n,&candidate)))continue;
                    PROPVARIANT name;PropVariantInit(&name);
                    if(SUCCEEDED(candidate->OpenPropertyStore(STGM_READ,&store)) && SUCCEEDED(store->GetValue(friendly_name,&name)) && name.vt==VT_LPWSTR) {
                        char narrow[256]{};WideCharToMultiByte(CP_UTF8,0,name.pwszVal,-1,narrow,sizeof(narrow)-1,nullptr,nullptr);
                        if(std::strstr(narrow,want)){device=candidate;candidate=nullptr;std::fprintf(stderr,"Host audio device (WASAPI): %s\n",narrow);}
                    }
                    PropVariantClear(&name);
                    if(store)store->Release();
                    if(candidate)candidate->Release();
                }
                list->Release();
            }
            if(!device)std::fprintf(stderr,"HG_AUDIO_DEVICE '%s' not found; using the default output\n",want);
        }
        if(!device && FAILED(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device))){enumerator->Release();return false;}
        enumerator->Release();
        IAudioClient* client=nullptr;
        const HRESULT activated=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&client));
        device->Release();
        if(FAILED(activated))return false;
        WAVEFORMATEX format{};
        format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=channels;format.nSamplesPerSec=sample_rate;
        format.wBitsPerSample=16;format.nBlockAlign=channels*sizeof(std::uint16_t);
        format.nAvgBytesPerSec=format.nSamplesPerSec*format.nBlockAlign;
        const DWORD flags=AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        const REFERENCE_TIME duration=REFERENCE_TIME(buffer_count)*chunk_frames()*10000000/sample_rate;
        UINT32 frames=0;IAudioRenderClient* render=nullptr;
        if(FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED,flags,duration,0,&format,nullptr)) ||
           FAILED(client->GetBufferSize(&frames)) ||
           FAILED(client->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(&render)))) {
            client->Release();return false;
        }
        client_=client;render_=render;client_frames_=frames;
        std::fprintf(stderr,"WASAPI buffer frames=%u chunk=%u\n",frames,chunk_frames());
        write_buffer_=write_frames_=submitted_buffers_=0;started_=starved_=false;
        return true;
    }
    unsigned client_padding() const {
        UINT32 padding=0;
        if(client_ && FAILED(client_->GetCurrentPadding(&padding)))return 0;
        return padding;
    }
    void close_device() noexcept {
        if(client_) {
            client_->Stop();
            if(render_)render_->Release();
            client_->Release();
            client_=nullptr;render_=nullptr;client_frames_=0;
            return;
        }
        if(!device_)return;
        waveOutReset(device_);
        for(auto& header:headers_)waveOutUnprepareHeader(device_,&header,sizeof(header));
        waveOutClose(device_);
        device_=nullptr;headers_={};
    }
    void lose_device(const std::string& why) noexcept {
        ++device_losses_;
        std::fprintf(stderr,"Host audio output lost (%s); continuing silently and retrying\n",why.c_str());
        close_device();
        retry_countdown_=sample_rate;
    }
    bool wait_until_free(unsigned index) {
        if(client_)return true; // WASAPI: submit() waits for room in the device buffer.
        auto& header=headers_[index];
        // A dead device can leave buffers queued forever; treat 2 s as device loss.
        for(unsigned waited=0;header.dwFlags&WHDR_INQUEUE;++waited) {
            if(waited>=2000){lose_device("audio buffer not returned for 2 s");return false;}
            observe_queue();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
    unsigned queued_buffers() const {
        if(client_) {const auto padding=client_padding();return (padding+chunk_frames()-1)/chunk_frames();}
        unsigned queued=0;
        for(const auto& header:headers_)queued+=(header.dwFlags&WHDR_INQUEUE)!=0;
        return queued;
    }
    void observe_queue() {
        const auto queued=queued_buffers();
        if(queued>max_queued_buffers_)max_queued_buffers_=queued;
        if(started_ && !queued) {
            if(!starved_)++underruns_;
            starved_=true;
        } else if(queued)starved_=false;
    }
    void start_playback() {
        if(client_) {
            if(FAILED(client_->Start())){lose_device("cannot start WASAPI output");return;}
            started_=true;starved_=false;observe_queue();return;
        }
        const auto result=waveOutRestart(device_);
        if(result!=MMSYSERR_NOERROR){lose_device(error_text("cannot start Windows audio output",result));return;}
        started_=true;starved_=false;observe_queue();
    }
    void submit() {
        auto& header=headers_[write_buffer_];
        if(client_) {
            const auto chunk=chunk_frames();
            header.lpData=reinterpret_cast<LPSTR>(buffers_[write_buffer_].data());
            header.dwBufferLength=DWORD(chunk*channels*sizeof(std::uint16_t));
            // Wait for room for one chunk; a device that stops consuming for 2 s is lost.
            for(unsigned waited=0;client_frames_-client_padding()<chunk;++waited) {
                if(waited>=2000){lose_device("WASAPI buffer not consumed for 2 s");return;}
                if(waited==200)std::fprintf(stderr,"WASAPI waiting: padding=%u frames=%u started=%d submitted=%u\n",client_padding(),client_frames_,int(started_),submitted_buffers_);
                observe_queue();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            dump_submit(header);
            BYTE* target=nullptr;
            if(FAILED(render_->GetBuffer(chunk,&target))){lose_device("cannot get WASAPI buffer");return;}
            std::memcpy(target,buffers_[write_buffer_].data(),header.dwBufferLength);
            if(FAILED(render_->ReleaseBuffer(chunk,0))){lose_device("cannot release WASAPI buffer");return;}
            ++submitted_buffers_;
            write_buffer_=(write_buffer_+1)%buffer_count;write_frames_=0;
            observe_queue();
            if(!started_ && submitted_buffers_>=prebuffer_count)start_playback();
            return;
        }
        dump_submit(header);
        const auto result=waveOutWrite(device_,&header,sizeof(header));
        if(result!=MMSYSERR_NOERROR){lose_device(error_text("cannot submit Windows audio buffer",result));return;}
        ++submitted_buffers_;
        write_buffer_=(write_buffer_+1)%buffer_count;write_frames_=0;
        observe_queue();
        if(!started_ && submitted_buffers_>=prebuffer_count)start_playback();
    }
    // HG-DIAG-128: HG_SPEAKER_DUMP=prefix writes every submitted buffer (exact PCM sent to
    // waveOut, after gain) to prefix.pcm and one CSV line per submit (QPC microseconds,
    // queued buffers before submit) to prefix.csv. Off by default; host-only.
    void dump_submit(const WAVEHDR& header) {
        static FILE* pcm=nullptr;static FILE* csv=nullptr;static bool checked=false;
        if(!checked) {
            checked=true;
            if(const char* prefix=std::getenv("HG_SPEAKER_DUMP")) {
                pcm=std::fopen((std::string(prefix)+".pcm").c_str(),"wb");
                csv=std::fopen((std::string(prefix)+".csv").c_str(),"w");
            }
        }
        if(!pcm)return;
        std::fwrite(header.lpData,1,header.dwBufferLength,pcm);
        LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
        std::fprintf(csv,"%lld,%u\n",static_cast<long long>(now.QuadPart*1000000/freq.QuadPart),queued_buffers());
    }
    void drain() {
        if(client_) {for(unsigned waited=0;client_ && client_padding() && waited<2000;++waited)std::this_thread::sleep_for(std::chrono::milliseconds(1));return;}
        for(unsigned n=0;n<buffer_count && device_;++n)if(!wait_until_free(n))return;
    }
    HWAVEOUT device_=nullptr;
    IAudioClient* client_=nullptr;
    IAudioRenderClient* render_=nullptr;
    UINT32 client_frames_=0;
    std::array<std::array<std::uint16_t,max_frames_per_buffer*channels>,buffer_count> buffers_{};
    std::array<WAVEHDR,buffer_count> headers_{};
    unsigned write_buffer_=0,write_frames_=0;
    unsigned submitted_buffers_=0,max_queued_buffers_=0;
    unsigned input_peak_=0,output_peak_=0;
    unsigned retry_countdown_=0;
    bool started_=false,starved_=false;
#endif
    std::uint64_t frames_=0;
    std::uint64_t underruns_=0;
    std::uint64_t dropped_frames_=0,device_losses_=0;
#ifndef _WIN32
    unsigned input_peak_=0,output_peak_=0;
#endif
};

}
