// SPDX-License-Identifier: MIT
// Quest pose diagnostic and paired calibration recorder. Visible mode submits
// black stereo projections so Virtual Desktop activates its PCVR input path.
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <unknwn.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef VDXR_WITH_SURVIVE
#include <libsurvive/survive_api.h>
#endif

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void xrcheck(XrResult result, const char* message) {
    if (XR_FAILED(result)) throw std::runtime_error(std::string(message)+": "+std::to_string(result));
}
template<class T> T get(PFN_xrGetInstanceProcAddr api, XrInstance instance, const char* name) {
    PFN_xrVoidFunction fn;
    xrcheck(api(instance,name,&fn), name);
    return reinterpret_cast<T>(fn);
}
#define API(name) const auto name = get<PFN_##name>(api, instance, #name)
int main(int argc, char** argv) try {
    std::cout << std::unitbuf;
    require(argc == 2 || argc == 3 || argc == 5, "Usage: tracker-quest-probe RUNTIME_DLL [--visible | HARDWARE_SERIAL left|right OUTPUT_CSV]");
    const bool capture = argc == 5;
    if(argc==3) require(std::string(argv[2])=="--visible","Unknown diagnostic option");
    const bool visible = capture || (argc==3 && std::string(argv[2])=="--visible");
    const int captureHand = capture && std::string(argv[3]) == "right" ? 1 : 0;
    if (capture) require(std::string(argv[3]) == "left" || std::string(argv[3]) == "right", "Choose left or right controller");
    std::ofstream output;
    uint32_t captured = 0;
#ifdef VDXR_WITH_SURVIVE
    std::unique_ptr<SurviveSimpleContext, decltype(&survive_simple_close)> survive(nullptr, survive_simple_close);
    if (capture) {
        char config[]="--configfile"; char path[]="./capture-lighthouse.json";
        require(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES,"Save Lighthouse calibration to ./capture-lighthouse.json before recording");
        SetEnvironmentVariableA("SURVIVE_GLOBALSCENESOLVER","0");
        char* args[]{argv[0],config,path};
        survive.reset(survive_simple_init(3,args));
        require(survive != nullptr,"Initialize libsurvive capture");
        survive_simple_start_thread(survive.get());
        output.open(argv[4]); require(bool(output),"Open capture CSV");
        output << "sx,sy,sz,sqx,sqy,sqz,sqw,tx,ty,tz,tqx,tqy,tqz,tqw,age_seconds\n" << std::setprecision(10);
    }
#else
    require(!capture,"Build with SURVIVE_SOURCE and SURVIVE_LIBRARY for capture");
#endif
    HMODULE dll = LoadLibraryA(argv[1]);
    require(dll != nullptr, "Load runtime DLL");
    auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderRuntimeInterface>(GetProcAddress(dll,"xrNegotiateLoaderRuntimeInterface"));
    require(negotiate != nullptr,"Find negotiation entry");
    XrNegotiateLoaderInfo loader{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO,XR_LOADER_INFO_STRUCT_VERSION,sizeof(XrNegotiateLoaderInfo),1,1,XR_MAKE_VERSION(1,0,0),XR_CURRENT_API_VERSION};
    XrNegotiateRuntimeRequest request{XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST,XR_RUNTIME_INFO_STRUCT_VERSION,sizeof(XrNegotiateRuntimeRequest)};
    xrcheck(negotiate(&loader,&request),"Negotiate");
    const auto api = request.getInstanceProcAddr;
    XrInstance instance = XR_NULL_HANDLE;
    API(xrCreateInstance);
    const char* extensions[]{XR_MND_HEADLESS_EXTENSION_NAME,XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME,XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(create.applicationInfo.applicationName,"VDXR real Quest pose diagnostic");
    create.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
    create.enabledExtensionCount=3; create.enabledExtensionNames=extensions;
    xrcheck(xrCreateInstance(&create,&instance),"Create instance");
    API(xrGetSystem); API(xrCreateSession); API(xrCreateReferenceSpace); API(xrPollEvent);
    API(xrBeginSession); API(xrLocateSpace); API(xrDestroySession); API(xrDestroyInstance);
    API(xrRequestExitSession); API(xrEndSession); API(xrDestroyActionSet);
    API(xrWaitFrame); API(xrBeginFrame); API(xrEndFrame);
    API(xrCreateSwapchain); API(xrEnumerateSwapchainImages); API(xrAcquireSwapchainImage);
    API(xrWaitSwapchainImage); API(xrReleaseSwapchainImage); API(xrLocateViews);
    API(xrConvertWin32PerformanceCounterToTimeKHR);
    API(xrStringToPath); API(xrCreateActionSet); API(xrCreateAction);
    API(xrSuggestInteractionProfileBindings); API(xrAttachSessionActionSets);
    API(xrCreateActionSpace); API(xrSyncActions);
    API(xrGetCurrentInteractionProfile); API(xrGetActionStatePose); API(xrEnumerateBoundSourcesForAction);
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system;
    xrcheck(xrGetSystem(instance,&systemInfo,&system),"Get connected Quest system");
    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(setInfo.actionSetName,"diagnostic"); std::strcpy(setInfo.localizedActionSetName,"Diagnostic");
    XrActionSet actionSet;
    xrcheck(xrCreateActionSet(instance,&setInfo,&actionSet),"Create action set");
    XrPath handPaths[2], bindingPaths[2], profile;
    xrcheck(xrStringToPath(instance,"/user/hand/left",&handPaths[0]),"Left path");
    xrcheck(xrStringToPath(instance,"/user/hand/right",&handPaths[1]),"Right path");
    xrcheck(xrStringToPath(instance,"/user/hand/left/input/grip/pose",&bindingPaths[0]),"Left grip");
    xrcheck(xrStringToPath(instance,"/user/hand/right/input/grip/pose",&bindingPaths[1]),"Right grip");
    xrcheck(xrStringToPath(instance,"/interaction_profiles/oculus/touch_controller",&profile),"Profile");
    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
    std::strcpy(actionInfo.actionName,"handpose"); std::strcpy(actionInfo.localizedActionName,"Hand pose");
    actionInfo.actionType=XR_ACTION_TYPE_POSE_INPUT; actionInfo.countSubactionPaths=2; actionInfo.subactionPaths=handPaths;
    XrAction action;
    xrcheck(xrCreateAction(actionSet,&actionInfo,&action),"Create action");
    XrActionSuggestedBinding bindings[]{{action,bindingPaths[0]},{action,bindingPaths[1]}};
    XrInteractionProfileSuggestedBinding suggestions{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggestions.interactionProfile=profile; suggestions.countSuggestedBindings=2; suggestions.suggestedBindings=bindings;
    xrcheck(xrSuggestInteractionProfileBindings(instance,&suggestions),"Bind controllers");
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO}; sessionInfo.systemId=system;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    XrGraphicsBindingD3D11KHR graphics{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    if(visible) {
        API(xrGetD3D11GraphicsRequirementsKHR);
        XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        xrcheck(xrGetD3D11GraphicsRequirementsKHR(instance,system,&requirements),"Get graphics requirements");
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"Create DXGI factory");
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        for(UINT index=0;;++index) {
            require(factory->EnumAdapters1(index,&adapter)!=DXGI_ERROR_NOT_FOUND,"Find runtime GPU");
            DXGI_ADAPTER_DESC1 description; adapter->GetDesc1(&description);
            if(std::memcmp(&description.AdapterLuid,&requirements.adapterLuid,sizeof(LUID))==0) break;
            adapter.Reset();
        }
        const auto level=requirements.minFeatureLevel;
        require(SUCCEEDED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,nullptr,nullptr)),"Create D3D11 device");
        graphics.device=device.Get(); sessionInfo.next=&graphics;
    }
    XrSession session;
    xrcheck(xrCreateSession(instance,&sessionInfo,&session),"Create headless session");
    XrSwapchain swapchains[2]{};
    std::vector<XrSwapchainImageD3D11KHR> images[2];
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if(visible) {
        device->GetImmediateContext(&context);
        for(int eye=0;eye<2;++eye) {
            XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            info.format=DXGI_FORMAT_R8G8B8A8_UNORM; info.sampleCount=1;
            info.width=info.height=256; info.faceCount=info.arraySize=info.mipCount=1;
            xrcheck(xrCreateSwapchain(session,&info,&swapchains[eye]),"Create diagnostic eye swapchain");
            uint32_t count=0;
            xrcheck(xrEnumerateSwapchainImages(swapchains[eye],0,&count,nullptr),"Count eye images");
            images[eye].resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            xrcheck(xrEnumerateSwapchainImages(swapchains[eye],count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images[eye].data())),"Read eye images");
        }
    }
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO}; attach.countActionSets=1; attach.actionSets=&actionSet;
    xrcheck(xrAttachSessionActionSets(session,&attach),"Attach actions");
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.poseInReferenceSpace.orientation.w=1;
    spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;
    XrSpace stage,view,hands[2];
    xrcheck(xrCreateReferenceSpace(session,&spaceInfo,&stage),"Create STAGE");
    spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
    xrcheck(xrCreateReferenceSpace(session,&spaceInfo,&view),"Create VIEW");
    for(int i=0;i<2;++i) {
        XrActionSpaceCreateInfo handInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        handInfo.action=action; handInfo.subactionPath=handPaths[i]; handInfo.poseInActionSpace.orientation.w=1;
        xrcheck(xrCreateActionSpace(session,&handInfo,&hands[i]),"Create controller space");
    }
    bool running=false;
    uint32_t validHead=0;
    const auto start=std::chrono::steady_clock::now();
    std::cout << (capture ? "Recording paired poses for 60 seconds; hold the tracker rigidly against the selected controller and move both together.\n" : "Reading runtime-reported poses for 15 seconds.\n");
    uint32_t iteration=0;
    while(std::chrono::steady_clock::now()-start < std::chrono::seconds(capture ? 60 : 15)) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while(xrPollEvent(instance,&event)==XR_SUCCESS) {
            if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto state=reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
                std::cout<<"Session state "<<state<<"\n";
                if(state==XR_SESSION_STATE_READY && !running) {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO}; begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    xrcheck(xrBeginSession(session,&begin),"Begin headless session"); running=true;
                }
            }
            event={XR_TYPE_EVENT_DATA_BUFFER};
        }
        if(running) {
            // VDXR's OVR backend establishes its frame transport on the first
            // wait. Merely opening a headless session can return placeholder
            // device data, even while the Quest app is streaming normally.
            XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
            XrFrameState frame{XR_TYPE_FRAME_STATE};
            xrcheck(xrWaitFrame(session,&waitInfo,&frame),"Wait headless frame");
            XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
            xrcheck(xrBeginFrame(session,&beginInfo),"Begin headless frame");
            XrActiveActionSet active{actionSet,XR_NULL_PATH};
            XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO}; sync.countActiveActionSets=1; sync.activeActionSets=&active;
            const auto syncResult=xrSyncActions(session,&sync);
            xrcheck(syncResult,"Sync actions");
            if(iteration==5) {
                XrBoundSourcesForActionEnumerateInfo bound{XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO};
                bound.action=action; uint32_t count=0;
                xrcheck(xrEnumerateBoundSourcesForAction(session,&bound,0,&count,nullptr),"Enumerate bindings");
                std::cout<<"Bound hand pose sources: "<<count<<"\n";
                for(int side=0;side<2;++side) {
                    XrInteractionProfileState profileState{XR_TYPE_INTERACTION_PROFILE_STATE};
                    xrcheck(xrGetCurrentInteractionProfile(session,handPaths[side],&profileState),"Read interaction profile");
                    XrActionStateGetInfo stateInfo{XR_TYPE_ACTION_STATE_GET_INFO};
                    stateInfo.action=action; stateInfo.subactionPath=handPaths[side];
                    XrActionStatePose poseState{XR_TYPE_ACTION_STATE_POSE};
                    xrcheck(xrGetActionStatePose(session,&stateInfo,&poseState),"Read pose action state");
                    std::cout<<"Hand "<<side<<" profile="<<profileState.interactionProfile<<" active="<<poseState.isActive<<"\n";
                }
            }
            LARGE_INTEGER counter; QueryPerformanceCounter(&counter); XrTime time;
            xrcheck(xrConvertWin32PerformanceCounterToTimeKHR(instance,&counter,&time),"Convert time");
            const XrSpace spaces[]{view,hands[0],hands[1]};
            const char* names[]{"head","left_controller","right_controller"};
            for(int i=0;i<3;++i) {
                XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
                xrcheck(xrLocateSpace(spaces[i],stage,time,&location),"Locate real pose");
                const auto& p=location.pose;
                if(!capture || iteration%50==0) std::cout<<names[i]<<" flags="<<location.locationFlags<<" "<<p.position.x<<" "<<p.position.y<<" "<<p.position.z
                    <<" "<<p.orientation.x<<" "<<p.orientation.y<<" "<<p.orientation.z<<" "<<p.orientation.w<<"\n";
                if(i==0 && (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) ++validHead;
            }
#ifdef VDXR_WITH_SURVIVE
            if(capture && syncResult==XR_SUCCESS) {
                if(iteration%50==0) {
                    std::cout<<"Paired samples captured: "<<captured<<"\n";
                    output.flush();
                }
                // Do not run another provider while this process owns the receiver.
                const auto* object=survive_simple_get_next_updated(survive.get());
                if(object && survive_simple_object_get_type(object)==SurviveSimpleObject_OBJECT &&
                    std::string(survive_simple_serial_number(object))==argv[2]) {
                    SurvivePose raw;
                    const double sampleTime=survive_simple_object_get_latest_pose(object,&raw);
                    const double age=survive_simple_run_time_since_epoch(survive.get())-sampleTime;
                    if(sampleTime>0 && age>=0 && age<0.05) {
                        XrSpaceLocation target{XR_TYPE_SPACE_LOCATION};
                        xrcheck(xrLocateSpace(hands[captureHand],stage,time-XrTime(age*1e9),&target),"Locate matched controller");
                        const auto valid=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
                            XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
                        if((target.locationFlags&valid)==valid) {
                            const auto& p=target.pose;
                            output<<raw.Pos[0]<<","<<raw.Pos[1]<<","<<raw.Pos[2]<<","<<raw.Rot[1]<<","<<raw.Rot[2]<<","<<raw.Rot[3]<<","<<raw.Rot[0]<<","
                                <<p.position.x<<","<<p.position.y<<","<<p.position.z<<","<<p.orientation.x<<","<<p.orientation.y<<","<<p.orientation.z<<","<<p.orientation.w<<","<<age<<"\n";
                            ++captured;
                        }
                    }
                }
            }
#endif
            XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
            endInfo.displayTime=frame.predictedDisplayTime;
            endInfo.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
            XrCompositionLayerProjectionView projectionViews[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
            const XrCompositionLayerBaseHeader* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
            if(visible) {
                XrView views[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
                XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
                locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                locate.displayTime=frame.predictedDisplayTime; locate.space=stage;
                XrViewState viewState{XR_TYPE_VIEW_STATE}; uint32_t count=0;
                xrcheck(xrLocateViews(session,&locate,&viewState,2,&count,views),"Locate stereo views");
                for(int eye=0;eye<2;++eye) {
                    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO}; uint32_t index;
                    xrcheck(xrAcquireSwapchainImage(swapchains[eye],&acquire,&index),"Acquire eye image");
                    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout=XR_INFINITE_DURATION;
                    xrcheck(xrWaitSwapchainImage(swapchains[eye],&wait),"Wait eye image");
                    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
                    D3D11_TEXTURE2D_DESC textureDescription;
                    images[eye][index].texture->GetDesc(&textureDescription);
                    D3D11_RENDER_TARGET_VIEW_DESC targetDescription{};
                    targetDescription.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
                    targetDescription.ViewDimension=textureDescription.ArraySize>1 ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;
                    if(textureDescription.ArraySize>1) targetDescription.Texture2DArray.ArraySize=1;
                    require(SUCCEEDED(device->CreateRenderTargetView(images[eye][index].texture,&targetDescription,&target)),"Create diagnostic render target");
                    const float black[]{0,0,0,1}; context->ClearRenderTargetView(target.Get(),black);
                    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    xrcheck(xrReleaseSwapchainImage(swapchains[eye],&release),"Release eye image");
                    projectionViews[eye].pose=views[eye].pose; projectionViews[eye].fov=views[eye].fov;
                    projectionViews[eye].subImage.swapchain=swapchains[eye];
                    projectionViews[eye].subImage.imageRect.extent={256,256};
                }
                projection.space=stage; projection.viewCount=2; projection.views=projectionViews;
                endInfo.layerCount=1; endInfo.layers=&layer;
            }
            xrcheck(xrEndFrame(session,&endInfo),"End headless frame");
        }
        ++iteration;
        Sleep(capture ? 20 : 500);
    }
    if(running) {
        xrcheck(xrRequestExitSession(session),"Request diagnostic exit");
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while(xrPollEvent(instance,&event)==XR_SUCCESS) event={XR_TYPE_EVENT_DATA_BUFFER};
        xrcheck(xrEndSession(session),"End diagnostic session");
    }
    xrcheck(xrDestroySession(session),"Destroy session");
    xrcheck(xrDestroyActionSet(actionSet),"Destroy diagnostic actions");
    xrcheck(xrDestroyInstance(instance),"Destroy instance");
    FreeLibrary(dll);
    require(validHead>0,"No runtime-reported valid headset samples; diagnostic is inconclusive");
    std::cout<<"Runtime-reported valid headset samples: "<<validHead<<"; paired calibration samples: "<<captured<<". This does not verify physical accuracy or tracker alignment.\n";
    if(capture) require(captured>=30,"Insufficient matched poses; investigate diagnostic bindings/transport and source calibration");
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
