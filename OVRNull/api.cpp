// MIT License
//
// Copyright(c) 2024-2026 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.h"

#include "log.h"
#include "driver.h"
#include "utils.h"

using namespace ovrnull::driver;
using namespace ovrnull::log;
using namespace ovrnull::utils;

namespace {

    ovrResult g_lastError = ovrError_NotInitialized;

    ovrResult OvrResultWrapper(ovrResult result) {
        g_lastError = result;
        return result;
    }

} // namespace

OVR_PUBLIC_FUNCTION(ovrResult) ovr_Initialize(const ovrInitParams* inputParams) {
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(void) ovr_Shutdown() {
    // TODO: Delete all Driver instances.
}

OVR_PUBLIC_FUNCTION(void) ovr_GetLastErrorInfo(ovrErrorInfo* errorInfo) {
    if (errorInfo) {
        errorInfo->Result = g_lastError;
        sprintf_s(errorInfo->ErrorString, sizeof(errorInfo->ErrorString), "ovrResult_%d", (int)errorInfo->Result);
    }
}

OVR_PUBLIC_FUNCTION(const char*) ovr_GetVersionString() {
    return "OVRNull";
}

OVR_PUBLIC_FUNCTION(int) ovr_TraceMessage(int level, const char* message) {
    TraceLoggingWrite(g_traceProvider, "Log", TLArg(message, "Message"), TLArg("OVR", "Origin"));
    return (int)strlen(message);
}

OVR_PUBLIC_FUNCTION(ovrResult) ovr_Create(ovrSession* pSession, ovrGraphicsLuid* pLuid) {
    if(!pSession || !pLuid)return OvrResultWrapper(ovrError_InvalidParameter);
    *pSession=nullptr;
    IDriver* interop=nullptr;
    try{interop=CreateDriver();}catch(const std::exception& e){
        TraceLoggingWrite(g_traceProvider,"OVR_Create_Failed",TLArg(e.what(),"Error"));
        return OvrResultWrapper(ovrError_NoHmd);
    }

    const LUID adapterLuid = interop->GetAdapterLuid();
    memcpy(pLuid, &adapterLuid, sizeof(LUID));
    *pSession = (ovrSession)interop;

    return OvrResultWrapper(ovrSuccess);
}

#define DECLARE_INTEROP(interop) IDriver* const interop = (IDriver*)session;

OVR_PUBLIC_FUNCTION(void) ovr_Destroy(ovrSession session) {
    DECLARE_INTEROP(interop);
    delete interop;
}

OVR_PUBLIC_FUNCTION(double) ovr_GetTimeInSeconds() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return QpcToOvrTime(now);
}

OVR_PUBLIC_FUNCTION(ovrHmdDesc) ovr_GetHmdDesc(ovrSession session) {
    DECLARE_INTEROP(interop);
    ovrHmdDesc hmdDesc{};
    hmdDesc.Type = ovrHmd_None;
    if (interop) {
        hmdDesc.Type = ovrHmd_Other;
        sprintf_s(hmdDesc.ProductName, sizeof(hmdDesc.ProductName), "%s", interop->GetModelNumber().c_str());
        sprintf_s(hmdDesc.Manufacturer, sizeof(hmdDesc.Manufacturer), "%s", interop->GetManufacturerName().c_str());
        sprintf_s(hmdDesc.SerialNumber, sizeof(hmdDesc.SerialNumber), "0000");
        hmdDesc.Resolution = interop->GetRecommendedResolution();
        hmdDesc.DefaultTrackingCaps = hmdDesc.AvailableTrackingCaps =
            ovrTrackingCap_Orientation | ovrTrackingCap_Position;
        hmdDesc.DefaultEyeFov[ovrEye_Left] = hmdDesc.MaxEyeFov[ovrEye_Left] = interop->GetEyeFov(ovrEye_Left);
        hmdDesc.DefaultEyeFov[ovrEye_Right] = hmdDesc.MaxEyeFov[ovrEye_Right] = interop->GetEyeFov(ovrEye_Right);
        hmdDesc.DisplayRefreshRate = interop->GetDisplayRate();
    }
    return hmdDesc;
}

OVR_PUBLIC_FUNCTION(ovrEyeRenderDesc)
ovr_GetRenderDesc(ovrSession session, ovrEyeType eyeType, ovrFovPort fov) {
    DECLARE_INTEROP(interop);
    ovrEyeRenderDesc renderDesc{};
    renderDesc.Eye = eyeType;
    renderDesc.Fov = interop->GetEyeFov(eyeType);
    renderDesc.DistortedViewport.Size = interop->GetRecommendedResolution();
    renderDesc.PixelsPerTanAngleAtCenter.x =
        renderDesc.DistortedViewport.Size.w / (renderDesc.Fov.LeftTan + renderDesc.Fov.RightTan);
    renderDesc.PixelsPerTanAngleAtCenter.y =
        renderDesc.DistortedViewport.Size.h / (renderDesc.Fov.UpTan + renderDesc.Fov.DownTan);
    renderDesc.HmdToEyePose = interop->GetEyePose(eyeType);
    return renderDesc;
}

OVR_PUBLIC_FUNCTION(ovrSizei)
ovr_GetFovTextureSize(ovrSession session, ovrEyeType eye, ovrFovPort fov, float pixelsPerDisplayPixel) {
    DECLARE_INTEROP(interop);
    const ovrSizei recommendedResolution = interop->GetRecommendedResolution();
    const ovrFovPort fullFov = interop->GetEyeFov(eye);
    ovrSizei size{};
    size.w = int(recommendedResolution.w * (fullFov.RightTan + fullFov.LeftTan) / (fov.RightTan + fov.LeftTan) *
                 pixelsPerDisplayPixel);
    size.w = ((size.w + 1) / 2) * 2;

    size.h = int(recommendedResolution.h * (fullFov.DownTan + fullFov.UpTan) / (fov.DownTan + fov.UpTan) *
                 pixelsPerDisplayPixel);
    size.h = ((size.h + 1) / 2) * 2;
    return size;
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetFovStencil(ovrSession session, const ovrFovStencilDesc* fovStencilDesc, ovrFovStencilMeshBuffer* meshBuffer) {
    DECLARE_INTEROP(interop);
    meshBuffer->UsedVertexCount = meshBuffer->UsedIndexCount = 0;
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetSessionStatus(ovrSession session, ovrSessionStatus* sessionStatus) {
    DECLARE_INTEROP(interop);
    const bool connected=interop->IsConnected();
    sessionStatus->IsVisible = connected;
    sessionStatus->HmdPresent = connected;
    sessionStatus->HmdMounted = connected;
    sessionStatus->DisplayLost = !connected;
    sessionStatus->ShouldQuit = false;
    sessionStatus->ShouldRecenter = false;
    sessionStatus->HasInputFocus = connected;
    sessionStatus->OverlayPresent = false;
    sessionStatus->DepthRequested = false;
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_SetTrackingOriginType(ovrSession session, ovrTrackingOrigin origin) {
    DECLARE_INTEROP(interop);
    interop->SetStageTracking(origin != ovrTrackingOrigin_EyeLevel);
    return ovrSuccess;
}

OVR_PUBLIC_FUNCTION(ovrTrackingOrigin) ovr_GetTrackingOriginType(ovrSession session) {
    DECLARE_INTEROP(interop);
    return interop->IsStageTracking() ? ovrTrackingOrigin_FloorLevel : ovrTrackingOrigin_EyeLevel;
}

OVR_PUBLIC_FUNCTION(ovrTrackingState)
ovr_GetTrackingState(ovrSession session, double absTime, ovrBool latencyMarker) {
    DECLARE_INTEROP(interop);
    ovrTrackingState trackingState{};
    ovrTrackedDeviceType type = ovrTrackedDevice_HMD;
    if (OVR_UNQUALIFIED_SUCCESS(ovr_GetDevicePoses(session, &type, 1, absTime, &trackingState.HeadPose))) {
        trackingState.StatusFlags = ovrStatus_OrientationValid | ovrStatus_OrientationTracked |
                                    ovrStatus_PositionValid | ovrStatus_PositionTracked;
    }
    type = ovrTrackedDevice_LTouch;
    if (OVR_UNQUALIFIED_SUCCESS(
            ovr_GetDevicePoses(session, &type, 1, absTime, &trackingState.HandPoses[ovrHand_Left]))) {
        trackingState.HandStatusFlags[ovrHand_Left] = ovrStatus_OrientationValid | ovrStatus_OrientationTracked |
                                                      ovrStatus_PositionValid | ovrStatus_PositionTracked;
    }
    type = ovrTrackedDevice_RTouch;
    if (OVR_UNQUALIFIED_SUCCESS(
            ovr_GetDevicePoses(session, &type, 1, absTime, &trackingState.HandPoses[ovrHand_Right]))) {
        trackingState.HandStatusFlags[ovrHand_Right] = ovrStatus_OrientationValid | ovrStatus_OrientationTracked |
                                                       ovrStatus_PositionValid | ovrStatus_PositionTracked;
    }
    trackingState.CalibratedOrigin = OVR::Posef::Identity();
    return trackingState;
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetDevicePoses(ovrSession session,
                   ovrTrackedDeviceType* deviceTypes,
                   int deviceCount,
                   double absTime,
                   ovrPoseStatef* outDevicePoses) {
    DECLARE_INTEROP(interop);
    if(!interop->IsConnected())return OvrResultWrapper(ovrError_DeviceUnavailable);
    for (int i = 0; i < deviceCount; i++) {
        switch (deviceTypes[i]) {
        case ovrTrackedDevice_HMD:
            outDevicePoses[i] = interop->GetHmdPose(absTime);
            break;
        case ovrTrackedDevice_LTouch:
            outDevicePoses[i] = interop->GetControllerPose(ovrHand_Left, absTime);
            break;
        case ovrTrackedDevice_RTouch:
            outDevicePoses[i] = interop->GetControllerPose(ovrHand_Right, absTime);
            break;
        default:
            return OvrResultWrapper(ovrError_DeviceUnavailable);
        }
        const auto& q=outDevicePoses[i].ThePose.Orientation;
        if(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w<0.5f)return OvrResultWrapper(ovrError_DeviceUnavailable);
    }
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetInputState(ovrSession session, ovrControllerType controllerType, ovrInputState* inputState) {
    DECLARE_INTEROP(interop);
    *inputState = {};
    if (controllerType & ovrControllerType_Touch) {
        *inputState = interop->GetControllerButtons();
    }
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(unsigned int) ovr_GetConnectedControllerTypes(ovrSession session) {
    DECLARE_INTEROP(interop);
    unsigned int types = 0;
    if (interop->HasController(ovrHand_Left)) {
        types |= ovrControllerType_LTouch;
    }
    if (interop->HasController(ovrHand_Right)) {
        types |= ovrControllerType_RTouch;
    }
    return types;
}

OVR_PUBLIC_FUNCTION(unsigned int)
ovr_GetFloatArray(ovrSession session,const char* name,float values[],unsigned int capacity){
    if(!session || !name || !values || capacity<7)return 0;
    const std::string_view key=name;
    if(key!="NightfallControllerAimLeft" && key!="NightfallControllerAimRight")return 0;
    DECLARE_INTEROP(interop);
    ovrPoseStatef pose{};
    if(!interop->GetControllerAimPose(key=="NightfallControllerAimLeft"?ovrHand_Left:ovrHand_Right,0,pose))return 0;
    memcpy(values,&pose.ThePose.Position,3*sizeof(float));memcpy(values+3,&pose.ThePose.Orientation,4*sizeof(float));
    return 7;
}

OVR_PUBLIC_FUNCTION(ovrBool) ovr_SetString(ovrSession session,const char* name,const char* value){
    if(!session || !name || !value || std::string_view(name)!="NightfallDisplayTime")return ovrFalse;
    char* end=nullptr;const double time=strtod(value,&end);
    if(!end || *end || !std::isfinite(time) || time<=0)return ovrFalse;
    DECLARE_INTEROP(interop);interop->SetFrameDisplayTime(time);return ovrTrue;
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_SetControllerVibration(ovrSession session, ovrControllerType controllerType, float frequency, float amplitude) {
    DECLARE_INTEROP(interop);
    if(controllerType & ovrControllerType_LTouch)interop->SetControllerVibration(ovrHand_Left,frequency,amplitude);
    if(controllerType & ovrControllerType_RTouch)interop->SetControllerVibration(ovrHand_Right,frequency,amplitude);
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_CreateTextureSwapChainDX(ovrSession session,
                             IUnknown* d3dPtr,
                             const ovrTextureSwapChainDesc* desc,
                             ovrTextureSwapChain* out_TextureSwapChain) {
    ComPtr<ID3D11Device> device;
    if (SUCCEEDED(d3dPtr->QueryInterface(device.ReleaseAndGetAddressOf()))) {
        DECLARE_INTEROP(interop);
        interop->SetSubmissionDevice(device.Get());
        *out_TextureSwapChain = (ovrTextureSwapChain)interop->CreateSwapchain(*desc);
        return OvrResultWrapper(ovrSuccess);
    }
    // This must mean DX12.
    TraceLoggingWrite(g_traceProvider, "OVR_CreateTextureSwapChainDX_Unsupported");
    return OvrResultWrapper(ovrError_Unsupported);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetTextureSwapChainLength(ovrSession session, ovrTextureSwapChain chain, int* out_Length) {
    DECLARE_INTEROP(interop);
    ovrTextureSwapChainDesc desc = interop->GetSwapchainDesc(chain);
    if (desc.Format == OVR_FORMAT_UNKNOWN) {
        return OvrResultWrapper(ovrError_InvalidParameter);
    }
    *out_Length = desc.StaticImage ? 1 : 3;
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetTextureSwapChainDesc(ovrSession session, ovrTextureSwapChain chain, ovrTextureSwapChainDesc* out_Desc) {
    DECLARE_INTEROP(interop);
    ovrTextureSwapChainDesc desc = interop->GetSwapchainDesc(chain);
    if (desc.Format == OVR_FORMAT_UNKNOWN) {
        return OvrResultWrapper(ovrError_InvalidParameter);
    }
    *out_Desc = desc;
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetTextureSwapChainBufferDX(ovrSession session, ovrTextureSwapChain chain, int index, IID iid, void** out_Buffer) {
    DECLARE_INTEROP(interop);
    ID3D11Texture2D* texture = interop->GetSwapchainImage(chain, index);
    if (!texture) {
        return OvrResultWrapper(ovrError_InvalidParameter);
    }
    texture->AddRef();
    *out_Buffer = texture;
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetTextureSwapChainCurrentIndex(ovrSession session, ovrTextureSwapChain chain, int* out_Index) {
    DECLARE_INTEROP(interop);
    int index = interop->GetSwapchainImageIndex(chain);
    if (index < 0) {
        return OvrResultWrapper(ovrError_InvalidParameter);
    }
    *out_Index = index;
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrResult) ovr_CommitTextureSwapChain(ovrSession session, ovrTextureSwapChain chain) {
    DECLARE_INTEROP(interop);
    if (!interop->CommitSwapchainImage(chain)) {
        return OvrResultWrapper(ovrError_InvalidParameter);
    }
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(void) ovr_DestroyTextureSwapChain(ovrSession session, ovrTextureSwapChain chain) {
    if (chain) {
        DECLARE_INTEROP(interop);
        interop->DestroySwapchain(chain);
    }
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_CreateMirrorTextureWithOptionsDX(ovrSession session,
                                     IUnknown* d3dPtr,
                                     const ovrMirrorTextureDesc* desc,
                                     ovrMirrorTexture* out_MirrorTexture) {
    ComPtr<ID3D11Device> device;
    if (SUCCEEDED(d3dPtr->QueryInterface(device.ReleaseAndGetAddressOf()))) {
        DECLARE_INTEROP(interop);
        interop->SetSubmissionDevice(device.Get());
        ovrTextureSwapChainDesc interopDesc{};
        interopDesc.Width = desc->Width;
        interopDesc.Height = desc->Height;
        interopDesc.Format = desc->Format;
        interopDesc.BindFlags = ovrTextureBind_DX_RenderTarget;
        interopDesc.MiscFlags = desc->MiscFlags;
        interopDesc.ArraySize = interopDesc.MipLevels = interopDesc.SampleCount = 1;
        interopDesc.StaticImage = true;
        *out_MirrorTexture = (ovrMirrorTexture)interop->CreateSwapchain(interopDesc);
        interop->SetMirrorTexture(*out_MirrorTexture);
        return OvrResultWrapper(ovrSuccess);
    }
    // This must mean DX12.
    TraceLoggingWrite(g_traceProvider, "OVR_CreateTextureSwapChainDX_Unsupported");
    return OvrResultWrapper(ovrError_Unsupported);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetMirrorTextureBufferDX(ovrSession session, ovrMirrorTexture mirrorTexture, IID iid, void** out_Buffer) {
    DECLARE_INTEROP(interop);
    ID3D11Texture2D* texture = interop->GetSwapchainImage(mirrorTexture, 0);
    if (!texture) {
        return OvrResultWrapper(ovrError_InvalidParameter);
    }
    texture->AddRef();
    *out_Buffer = texture;
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(void) ovr_DestroyMirrorTexture(ovrSession session, ovrMirrorTexture mirrorTexture) {
    if (mirrorTexture) {
        DECLARE_INTEROP(interop);
        interop->SetMirrorTexture(nullptr);
        interop->DestroySwapchain(mirrorTexture);
    }
}

OVR_PUBLIC_FUNCTION(ovrResult) ovr_WaitToBeginFrame(ovrSession session, long long frameIndex) {
    DECLARE_INTEROP(interop);
    interop->WaitForVsync(frameIndex);
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrResult) ovr_BeginFrame(ovrSession session, long long frameIndex) {
    DECLARE_INTEROP(interop);
    // No-op.
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_EndFrame(ovrSession session,
             long long frameIndex,
             const ovrViewScaleDesc* viewScaleDesc,
             ovrLayerHeader const* const* layerPtrList,
             unsigned int layerCount) {
    DECLARE_INTEROP(interop);
    std::vector<const ovrLayerHeader*> layers(layerPtrList, layerPtrList + layerCount);
    if (!interop->SubmitFrame(layers)) {
        return OvrResultWrapper(ovrError_InvalidParameter);
    }
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_SubmitFrame(ovrSession session,
                long long frameIndex,
                const ovrViewScaleDesc* viewScaleDesc,
                ovrLayerHeader const* const* layerPtrList,
                unsigned int layerCount) {
    // This is a legacy API, handle it as best as we could without too much extra work.
    ovrResult result = ovr_BeginFrame(session, frameIndex);
    if (OVR_UNQUALIFIED_SUCCESS(result)) {
        result = ovr_EndFrame(session, frameIndex, viewScaleDesc, layerPtrList, layerCount);
    }
    if (OVR_UNQUALIFIED_SUCCESS(result)) {
        result = ovr_WaitToBeginFrame(session, frameIndex);
    }
    return result;
}

OVR_PUBLIC_FUNCTION(double) ovr_GetPredictedDisplayTime(ovrSession session, long long frameIndex) {
    DECLARE_INTEROP(interop);
    return interop->GetPredictedDisplayTime(frameIndex);
}

OVR_PUBLIC_FUNCTION(ovrResult) ovr_GetPerfStats(ovrSession session, ovrPerfStats* outStats) {
    DECLARE_INTEROP(interop);
    *outStats = {};
    // VDXR only uses this information.
    outStats->FrameStatsCount = 1;
    outStats->FrameStats[0].AswIsActive = interop->IsAswActive();
    return OvrResultWrapper(ovrSuccess);
}

OVR_PUBLIC_FUNCTION(ovrBool) ovr_GetBool(ovrSession session, const char* propertyName, ovrBool defaultVal) {
    DECLARE_INTEROP(interop);

    // VDXR only uses these properties.
    const std::string_view prop = propertyName;
    if (prop == "SupportsEyeTracking") {
        return false;
    } else if (prop == "SupportsHandTracking") {
        return false;
    } else if (prop == "SupportsFaceTracking") {
        return false;
    } else if (prop == "SupportsBodyTracking") {
        return false;
    } else if (prop == "SupportsFullBodyTracking") {
        return false;
    } else if (prop == "EmulateTrackers") {
        return false;
    } else if (prop == "EmulateIndexControllers") {
        return false;
    }

    TraceLoggingWrite(g_traceProvider, "OVR_GetBool_Unsupported", TLArg(propertyName, "Property"));
    return defaultVal;
}

OVR_PUBLIC_FUNCTION(ovrBool) ovr_SetBool(ovrSession session, const char* propertyName, ovrBool value) {
    DECLARE_INTEROP(interop);

    // VDXR only uses these properties.
    const std::string_view prop = propertyName;
    if (prop == "IsVDXR") {
        return ovrTrue;
    } else if (prop == "IsOpenComposite") {
        return ovrTrue;
    }

    TraceLoggingWrite(g_traceProvider, "OVR_SetBool_Unsupported", TLArg(propertyName, "Property"));
    return ovrFalse;
}

OVR_PUBLIC_FUNCTION(float) ovr_GetFloat(ovrSession session, const char* propertyName, float defaultVal) {
    DECLARE_INTEROP(interop);

    // VDXR only uses these properties.
    const std::string_view property(propertyName);
    if (property == OVR_KEY_EYE_HEIGHT) {
        return interop->GetEyeHeight();
    }

    TraceLoggingWrite(g_traceProvider, "OVR_GetFloat_Unsupported", TLArg(propertyName, "Property"));
    return defaultVal;
}
