#pragma once

#include "CoreMinimal.h"
#include "CaptureOutputSettings.h"

struct FPanoramaCaptureFrame
{
    FPanoramaCaptureFrame() = default;

    FPanoramaCaptureFrame(const FIntPoint InResolution, const double InTimeSeconds, const int32 InFrameIndex, const FString& InOutputFile, bool bInIs16Bit, TArray<uint8>&& InPayload)
        : Resolution(InResolution)
        , TimeSeconds(InTimeSeconds)
        , FrameIndex(InFrameIndex)
        , OutputFile(InOutputFile)
        , bIs16Bit(bInIs16Bit)
        , Payload(MoveTemp(InPayload))
    {
    }

    FIntPoint Resolution = FIntPoint::ZeroValue;
    double TimeSeconds = 0.0;
    int32 FrameIndex = 0;
    FString OutputFile;
    bool bIs16Bit = false;
    TArray<uint8> Payload;
};

class PANORAMACAPTURE_API FCaptureFrameRingBuffer
{
public:
    FCaptureFrameRingBuffer();
    ~FCaptureFrameRingBuffer();

    void Initialize(int32 InCapacity, ERingBufferOverflowPolicy InPolicy);
    void Clear();

    bool Enqueue(FPanoramaCaptureFrame&& Frame);
    bool Dequeue(FPanoramaCaptureFrame& OutFrame);

    int32 Num() const;
    int32 Capacity() const;

    int32 GetDroppedFrames() const;
    int32 GetBlockedFrames() const;
    ERingBufferOverflowPolicy GetOverflowPolicy() const { return OverflowPolicy; }

private:
    mutable FCriticalSection CriticalSection;
    TArray<FPanoramaCaptureFrame> Frames;
    int32 Head;
    int32 Tail;
    int32 CurrentSize;
    int32 MaxCapacity;
    int32 DroppedFrames;
    int32 BlockedEnqueues;
    ERingBufferOverflowPolicy OverflowPolicy;
    FEvent* NotEmptyEvent;
    FEvent* NotFullEvent;
};
