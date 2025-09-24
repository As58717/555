#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CaptureOutputSettings.generated.h"

UENUM(BlueprintType)
enum class EPanoramaStereoMode : uint8
{
    Mono,
    StereoOverUnder,
    StereoSideBySide
};

UENUM(BlueprintType)
enum class ECaptureOutputPath : uint8
{
    PNGSequence,
    NVENCVideo
};

UENUM(BlueprintType)
enum class EEquiLayout : uint8
{
    Full360,
    UpperHemisphere,
    LowerHemisphere
};

UENUM(BlueprintType)
enum class EPanoramaGammaSpace : uint8
{
    sRGB,
    Linear
};

UENUM(BlueprintType)
enum class ERingBufferOverflowPolicy : uint8
{
    DropOldest,
    DropNewest,
    BlockUntilAvailable
};

UENUM(BlueprintType)
enum class ENVENCCodec : uint8
{
    H264,
    HEVC
};

UENUM(BlueprintType)
enum class ENVENCRateControlMode : uint8
{
    CBR,
    VBR,
    CQP
};

USTRUCT(BlueprintType)
struct FNVENCRateControl
{
    GENERATED_BODY()

    FNVENCRateControl()
        : Codec(ENVENCCodec::HEVC)
        , RateControlMode(ENVENCRateControlMode::CBR)
        , BitrateMbps(80.f)
        , MaxBitrateMbps(120.f)
        , GOPLength(30)
        , bEnableBFrames(true)
        , BFrameCount(2)
        , bUseP010(false)
        , bZeroLatency(false)
        , bAsyncTransfer(true)
        , AsyncDepth(4)
        , VBVMultiplier(1.0f)
    {
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC")
    ENVENCCodec Codec;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC")
    ENVENCRateControlMode RateControlMode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC", meta = (ClampMin = "1"))
    float BitrateMbps;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC", meta = (ClampMin = "0"))
    float MaxBitrateMbps;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC", meta = (ClampMin = "1"))
    int32 GOPLength;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC")
    bool bEnableBFrames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC", meta = (EditCondition = "bEnableBFrames", ClampMin = "0"))
    int32 BFrameCount;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC", meta = (ToolTip = "Encode using 10-bit P010 surfaces"))
    bool bUseP010;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC", meta = (ToolTip = "Disables lookahead for lowest latency"))
    bool bZeroLatency;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC", meta = (ToolTip = "Encode on a worker thread"))
    bool bAsyncTransfer;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC", meta = (ClampMin = "1"))
    int32 AsyncDepth;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC", meta = (ClampMin = "0.1"))
    float VBVMultiplier;
};

USTRUCT(BlueprintType)
struct FPanoramaCaptureResolution
{
    GENERATED_BODY()

    FPanoramaCaptureResolution()
        : Width(3840)
        , Height(2160)
    {
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    int32 Width;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    int32 Height;
};

USTRUCT(BlueprintType)
struct FCaptureOutputSettings
{
    GENERATED_BODY()

    FCaptureOutputSettings()
        : OutputPath(ECaptureOutputPath::PNGSequence)
        , StereoMode(EPanoramaStereoMode::Mono)
        , OutputLayout(EEquiLayout::Full360)
        , GammaSpace(EPanoramaGammaSpace::sRGB)
        , FrameRate(30)
        , bEmbedTimecode(true)
        , bRecordAudio(true)
        , bEnablePreview(true)
        , PreviewScale(0.25f)
        , bUseRingBuffer(true)
        , RingBufferPolicy(ERingBufferOverflowPolicy::DropOldest)
        , RingBufferDurationSeconds(4.f)
        , RingBufferCapacityOverride(0)
        , OutputDirectory(TEXT(""))
        , BaseFileName(TEXT("PanoramaCapture"))
        , ContainerFormat(TEXT("mp4"))
        , bAutoAssembleVideo(true)
        , bAutoMuxNVENC(true)
        , bPreferD3D12Interop(true)
        , MaxEncoderLatencyMs(120.0f)
        , FFmpegMuxOverride(TEXT(""))
    {
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    ECaptureOutputPath OutputPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EPanoramaStereoMode StereoMode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EEquiLayout OutputLayout;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EPanoramaGammaSpace GammaSpace;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FPanoramaCaptureResolution Resolution;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    int32 FrameRate;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    bool bEmbedTimecode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|Audio")
    bool bRecordAudio;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    bool bEnablePreview;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (EditCondition = "bEnablePreview", ClampMin = "0.1", ClampMax = "1.0"))
    float PreviewScale;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    bool bUseRingBuffer;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (EditCondition = "bUseRingBuffer"))
    ERingBufferOverflowPolicy RingBufferPolicy;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (EditCondition = "bUseRingBuffer"))
    float RingBufferDurationSeconds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (EditCondition = "bUseRingBuffer", ClampMin = "0"))
    int32 RingBufferCapacityOverride;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FString OutputDirectory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FString BaseFileName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FString ContainerFormat;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    bool bAutoAssembleVideo;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|PNG", meta = (EditCondition = "OutputPath == ECaptureOutputPath::PNGSequence"))
    bool bUse16BitPNG = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|NVENC", meta = (EditCondition = "OutputPath == ECaptureOutputPath::NVENCVideo"))
    bool bAutoMuxNVENC;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|NVENC", meta = (EditCondition = "OutputPath == ECaptureOutputPath::NVENCVideo"))
    bool bPreferD3D12Interop;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|NVENC", meta = (EditCondition = "OutputPath == ECaptureOutputPath::NVENCVideo", ClampMin = "1"))
    float MaxEncoderLatencyMs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|NVENC", meta = (EditCondition = "OutputPath == ECaptureOutputPath::NVENCVideo"))
    FString FFmpegMuxOverride;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|NVENC", meta = (EditCondition = "OutputPath == ECaptureOutputPath::NVENCVideo"))
    FNVENCRateControl NVENC;
};

UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "Panorama Capture"))
class UPanoramaCaptureSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPanoramaCaptureSettings();

    UPROPERTY(EditAnywhere, Config, Category = "Capture")
    FCaptureOutputSettings DefaultOutput;

    UPROPERTY(EditAnywhere, Config, Category = "Capture")
    FString NVENCProfile;

    UPROPERTY(EditAnywhere, Config, Category = "Audio")
    FName AudioSubmix;

    UPROPERTY(EditAnywhere, Config, Category = "Capture")
    FString DefaultOutputDirectory;

    UPROPERTY(EditAnywhere, Config, Category = "Capture")
    bool bDefaultAutoAssemble;

    UPROPERTY(EditAnywhere, Config, Category = "Capture")
    FString FFmpegExecutable;
};
