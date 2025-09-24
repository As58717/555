#pragma once

#include "CoreMinimal.h"
#include "CaptureOutputSettings.h"
#include "RHIResources.h"

/** Configuration passed to a hardware video encoder implementation. */
struct PANORAMACAPTURE_API FPanoramaVideoEncoderConfig
{
    FString OutputFile;
    FString ElementaryStreamFile;
    FCaptureOutputSettings OutputSettings;
    FIntPoint OutputResolution = FIntPoint::ZeroValue;
    int32 FrameRate = 30;
    bool bUseD3D12 = true;
    bool bUse10Bit = false;
};

struct PANORAMACAPTURE_API FPanoramaVideoEncoderStats
{
    double AverageLatencyMs = 0.0;
    int32 QueuedFrames = 0;
    int32 SubmittedFrames = 0;
    int32 EncodedFrames = 0;
    int32 DroppedFrames = 0;
};

struct PANORAMACAPTURE_API FPanoramaVideoEncoderFrame
{
    FRHITexture* RgbaTexture = nullptr;
    FRHITexture* LumaTexture = nullptr;
    FRHITexture* ChromaTexture = nullptr;
    double TimeSeconds = 0.0;
    bool bIsNV12 = false;
    bool bIsP010 = false;
};

/**
 * Lightweight interface implemented by platform-specific encoders (e.g. NVENC).
 */
class PANORAMACAPTURE_API IPanoramaVideoEncoder : public TSharedFromThis<IPanoramaVideoEncoder, ESPMode::ThreadSafe>
{
public:
    virtual ~IPanoramaVideoEncoder() = default;

    virtual bool Initialize(const FPanoramaVideoEncoderConfig& InConfig) = 0;
    virtual void EncodeFrame(const FPanoramaVideoEncoderFrame& Frame) = 0;
    virtual void Flush() = 0;
    virtual bool FinalizeEncoding(FString& OutElementaryStream) = 0;
    virtual FPanoramaVideoEncoderStats GetStats() const = 0;

    virtual void EncodeTexture(FRHITexture* Texture, double TimeSeconds)
    {
        FPanoramaVideoEncoderFrame Frame;
        Frame.RgbaTexture = Texture;
        Frame.TimeSeconds = TimeSeconds;
        EncodeFrame(Frame);
    }
};
