#pragma once

#include "CoreMinimal.h"
#include "VideoEncoder.h"

class PANORAMACAPTURENVENC_API FPanoramaNVENCEncoder : public IPanoramaVideoEncoder
{
public:
    FPanoramaNVENCEncoder();
    ~FPanoramaNVENCEncoder();

    virtual bool Initialize(const FPanoramaVideoEncoderConfig& InConfig) override;
    virtual void EncodeFrame(const FPanoramaVideoEncoderFrame& Frame) override;
    virtual void Flush() override;
    virtual bool FinalizeEncoding(FString& OutElementaryStream) override;
    virtual FPanoramaVideoEncoderStats GetStats() const override;

private:
    struct FRegisteredResource
    {
        FRHITexture* Texture = nullptr;
        void* RegisteredHandle = nullptr;
    };

    struct FEncodeSubmission
    {
        FPanoramaVideoEncoderFrame Frame;
    };

    bool InitializeSession();
    void ShutdownSession();
    bool RegisterIfNeeded(FRHITexture* Texture, void** OutRegisteredHandle);
    void EncodeSubmission(const FEncodeSubmission& Submission);
    void DrainBitstream(void* OutputBitstream, double Timestamp);
    void FlushPendingTasks();

    FPanoramaVideoEncoderConfig Config;
    bool bInitialized;
    bool bUsingD3D12;

    TUniquePtr<class FArchive> ElementaryStreamWriter;
    TMap<FRHITexture*, FRegisteredResource> RegisteredResources;
    TArray<void*> AvailableBitstreams;
    void* EncoderInterface;
    void* DeviceHandle;
    struct NV_ENCODE_API_FUNCTION_LIST* FunctionList;
    void* NvEncLibraryHandle;

    FGraphEventRef LastEncodeTask;
    mutable FCriticalSection EncodeCriticalSection;
    FPanoramaVideoEncoderStats Stats;
    double TotalEncodeLatencySeconds;
};
