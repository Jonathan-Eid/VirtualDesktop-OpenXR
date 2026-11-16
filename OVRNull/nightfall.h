// Copyright (c) 2026 Jonathan Eid. MIT License (see ../LICENSE).
#pragma once
#include "nightfall_abi.h"
#include <filesystem>
#include <stdexcept>
#include <memory>

// The host supplies an explicit absolute DLL path for each launched application.
// Without that opt-in, the original OVRNull diagnostic behavior is unchanged.
class NightfallProvider {
    HMODULE module=nullptr;
    void *context=nullptr;
    NightfallOvrCloseV1 close=nullptr;
    template<class T> T symbol(const char *name){
        auto f=reinterpret_cast<T>(GetProcAddress(module,name));
        if(!f)throw std::runtime_error(std::string("Missing Nightfall backend export: ")+name);
        return f;
    }
public:
    NightfallOvrConfigV1 config{};
    NightfallOvrSampleV1 sample=nullptr;
    NightfallOvrSubmitV1 submit=nullptr;
    NightfallOvrHapticV1 haptic=nullptr;
    static std::unique_ptr<NightfallProvider> Create(){
        std::wstring path(32768,L'\0');
        const DWORD n=GetEnvironmentVariableW(L"NIGHTFALL_OVR_BRIDGE",path.data(),DWORD(path.size()));
        if(!n)return nullptr;
        if(n>=path.size())throw std::runtime_error("Nightfall backend path is too long");
        path.resize(n);
        if(!std::filesystem::path(path).is_absolute() || !std::filesystem::is_regular_file(path))
            throw std::runtime_error("Nightfall backend requires an existing absolute DLL path");
        auto provider=std::make_unique<NightfallProvider>();
        provider->module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!provider->module)throw std::runtime_error("Cannot load Nightfall backend DLL");
        auto open=provider->symbol<NightfallOvrOpenV1>("nightfall_ovr_open_v1");
        provider->close=provider->symbol<NightfallOvrCloseV1>("nightfall_ovr_close_v1");
        provider->sample=provider->symbol<NightfallOvrSampleV1>("nightfall_ovr_sample_v1");
        provider->submit=provider->symbol<NightfallOvrSubmitV1>("nightfall_ovr_submit_v1");
        provider->haptic=provider->symbol<NightfallOvrHapticV1>("nightfall_ovr_haptic_v1");
        provider->config.size=sizeof(provider->config);
        provider->context=open(&provider->config);
        if(!provider->context || provider->config.size!=sizeof(provider->config) || provider->config.version!=1 ||
            provider->config.eye_width<256 || provider->config.eye_width>4096 || provider->config.eye_width%16 ||
            provider->config.eye_height<256 || provider->config.eye_height>4096 || provider->config.eye_height%16 ||
            (provider->config.refresh_hz!=20 && provider->config.refresh_hz!=60 && provider->config.refresh_hz!=72))
            throw std::runtime_error("Nightfall tracking or backend configuration is unavailable");
        return provider;
    }
    ~NightfallProvider(){if(context && close)close(context);if(module)FreeLibrary(module);}
    NightfallOvrStateV1 Sample(double at) const {
        if(at<=0){LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);at=double(now.QuadPart)/frequency.QuadPart;}
        NightfallOvrStateV1 state{};state.size=sizeof(state);
        if(!sample(context,int64_t(at*1e9),&state))state.head_valid=0;
        return state;
    }
    bool Submit(ID3D11Device *device,ID3D11Texture2D *texture,uint64_t frame,double displayTime,const float *views){
        return submit(context,device,texture,frame,uint64_t(displayTime*1e9),views)!=0;
    }
    void Haptic(unsigned side,float amplitude){haptic(context,side,amplitude,50000000);}
    static ovrPoseStatef Pose(const float *p,double at){
        ovrPoseStatef pose{};pose.ThePose.Position={p[0],p[1],p[2]};pose.ThePose.Orientation={p[3],p[4],p[5],p[6]};pose.TimeInSeconds=at;return pose;
    }
};
