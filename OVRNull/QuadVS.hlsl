// Copyright (c) 2026 Jonathan Eid. MIT License (see ../LICENSE).
#include "constantsbuffer.h"
cbuffer cb : register(b0) { ConstantsBuffer cb; };
float4 main(uint id : SV_VertexID,out float3 reprojectedCoord : TEXCOORD0) : SV_Position {
    const float2 corners[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    float2 ndc=corners[id]*float2(2,-2)+float2(-1,1);
    reprojectedCoord=float3(ndc,1);
    return mul(float4(ndc,0,1),cb.reprojectionMatrix);
}
