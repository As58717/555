#pragma once

#include "CoreMinimal.h"
#include "RenderGraphDefinitions.h"
#include "RHIResources.h"

struct FCubemapEquirectDispatchParams
{
    FRDGTextureRef SourceCubemapLeft = nullptr;
    FRDGTextureRef SourceCubemapRight = nullptr;
    FRDGTextureRef DestinationEquirect = nullptr;
    FIntPoint OutputResolution = FIntPoint(3840, 2160);
    bool bStereo = false;
    bool bLinearGamma = false;
    bool bStereoOverUnder = true;
};

class PANORAMACAPTURE_API FCubemapEquirectPass
{
public:
    static void AddComputePass(FRDGBuilder& GraphBuilder, const FCubemapEquirectDispatchParams& Params);
};
