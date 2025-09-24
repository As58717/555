#include "NVENCEncoder.h"

#include "PanoramaCaptureModule.h"
#include "PanoramaCaptureNVENCModule.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "RHIResources.h"

#if PLATFORM_WINDOWS && WITH_PANORAMA_NVENC
#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#include "nvEncodeAPI.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "D3D11RHIPrivate.h"
#include "D3D11RHI.h"
#include "D3D12RHIPrivate.h"
#include "D3D12RHI.h"
#endif

FPanoramaNVENCEncoder::FPanoramaNVENCEncoder()
    : bInitialized(false)
    , bUsingD3D12(false)
    , EncoderInterface(nullptr)
    , DeviceHandle(nullptr)
    , FunctionList(nullptr)
    , NvEncLibraryHandle(nullptr)
    , TotalEncodeLatencySeconds(0.0)
{
}

FPanoramaNVENCEncoder::~FPanoramaNVENCEncoder()
{
    FString DummyOutput;
    FinalizeEncoding(DummyOutput);
}

bool FPanoramaNVENCEncoder::Initialize(const FPanoramaVideoEncoderConfig& InConfig)
{
    Config = InConfig;
    Stats = FPanoramaVideoEncoderStats();
    TotalEncodeLatencySeconds = 0.0;

#if PLATFORM_WINDOWS && WITH_PANORAMA_NVENC
    if (!InitializeSession())
    {
        UE_LOG(LogPanoramaNVENC, Error, TEXT("Failed to initialize NVENC session."));
        ShutdownSession();
        return false;
    }

    IFileManager::Get().Delete(*Config.ElementaryStreamFile);
    ElementaryStreamWriter.Reset(IFileManager::Get().CreateFileWriter(*Config.ElementaryStreamFile, FILEWRITE_EvenIfReadOnly | FILEWRITE_AllowRead));
    if (!ElementaryStreamWriter)
    {
        UE_LOG(LogPanoramaNVENC, Error, TEXT("Unable to open elementary stream file '%s' for writing."), *Config.ElementaryStreamFile);
        ShutdownSession();
        return false;
    }

    bInitialized = true;
    UE_LOG(LogPanoramaNVENC, Log, TEXT("NVENC initialized: %dx%d @ %d FPS (%s)."),
        Config.OutputResolution.X,
        Config.OutputResolution.Y,
        Config.FrameRate,
        (Config.OutputSettings.NVENC.Codec == ENVENCCodec::HEVC) ? TEXT("HEVC") : TEXT("H.264"));
    return true;
#else
    UE_LOG(LogPanoramaNVENC, Warning, TEXT("NVENC initialization attempted on unsupported platform."));
    return false;
#endif
}

void FPanoramaNVENCEncoder::EncodeFrame(const FPanoramaVideoEncoderFrame& Frame)
{
#if PLATFORM_WINDOWS && WITH_PANORAMA_NVENC
    if (!bInitialized || (!Frame.RgbaTexture && !Frame.LumaTexture))
    {
        return;
    }

    Stats.SubmittedFrames++;

    FEncodeSubmission Submission;
    Submission.Frame = Frame;

    FGraphEventRef PrevTask = LastEncodeTask;
    LastEncodeTask = FFunctionGraphTask::CreateAndDispatchWhenReady([this, Submission]()
    {
        EncodeSubmission(Submission);
    }, TStatId(), PrevTask);
#else
    UE_LOG(LogPanoramaNVENC, Warning, TEXT("EncodeFrame called without NVENC support."));
#endif
}

void FPanoramaNVENCEncoder::Flush()
{
#if PLATFORM_WINDOWS && WITH_PANORAMA_NVENC
    FlushPendingTasks();
#endif
}

bool FPanoramaNVENCEncoder::FinalizeEncoding(FString& OutElementaryStream)
{
#if PLATFORM_WINDOWS && WITH_PANORAMA_NVENC
    if (!bInitialized)
    {
        OutElementaryStream.Reset();
        return false;
    }

    Flush();

    if (FunctionList && EncoderInterface)
    {
        FunctionList->nvEncFlushEncoderQueue(EncoderInterface, nullptr);
    }

    if (ElementaryStreamWriter)
    {
        ElementaryStreamWriter->Close();
        ElementaryStreamWriter.Reset();
    }

    OutElementaryStream = Config.ElementaryStreamFile;

    ShutdownSession();
    bInitialized = false;
    return true;
#else
    OutElementaryStream.Reset();
    return false;
#endif
}

FPanoramaVideoEncoderStats FPanoramaNVENCEncoder::GetStats() const
{
#if PLATFORM_WINDOWS && WITH_PANORAMA_NVENC
    FScopeLock Lock(&EncodeCriticalSection);
    FPanoramaVideoEncoderStats Result = Stats;
    Result.QueuedFrames = FMath::Max(0, Stats.SubmittedFrames - Stats.EncodedFrames);
    if (Stats.EncodedFrames > 0)
    {
        Result.AverageLatencyMs = (TotalEncodeLatencySeconds / static_cast<double>(Stats.EncodedFrames)) * 1000.0;
    }
    return Result;
#else
    return FPanoramaVideoEncoderStats();
#endif
}

#if PLATFORM_WINDOWS && WITH_PANORAMA_NVENC

namespace
{
    const GUID& GetCodecGuid(const FPanoramaVideoEncoderConfig& Config)
    {
        return (Config.OutputSettings.NVENC.Codec == ENVENCCodec::HEVC) ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;
    }

    NV_ENC_BUFFER_FORMAT GetBufferFormat(const FPanoramaVideoEncoderConfig& Config)
    {
        return Config.bUse10Bit ? NV_ENC_BUFFER_FORMAT_YUV420_10BIT : NV_ENC_BUFFER_FORMAT_NV12;
    }

    NV_ENC_PARAMS_RC_MODE GetRateControlMode(const FNVENCRateControl& RateControl)
    {
        switch (RateControl.RateControlMode)
        {
        case ENVENCRateControlMode::CQP:
            return NV_ENC_PARAMS_RC_CONSTQP;
        case ENVENCRateControlMode::VBR:
            return NV_ENC_PARAMS_RC_VBR;
        default:
            return NV_ENC_PARAMS_RC_CBR;
        }
    }
}

bool FPanoramaNVENCEncoder::InitializeSession()
{
    FString RHIName = GDynamicRHI ? GDynamicRHI->GetName() : TEXT("");
    bUsingD3D12 = Config.bUseD3D12 && RHIName.Contains(TEXT("D3D12"));

    if (bUsingD3D12)
    {
        FD3D12DynamicRHI* D3D12RHI = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);
        DeviceHandle = D3D12RHI ? D3D12RHI->RHIGetDevice() : nullptr;
    }
    else
    {
        FD3D11DynamicRHI* D3D11RHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);
        DeviceHandle = D3D11RHI ? D3D11RHI->RHIGetDevice() : nullptr;
    }

    if (!DeviceHandle)
    {
        UE_LOG(LogPanoramaNVENC, Error, TEXT("Failed to acquire native RHI device for NVENC."));
        return false;
    }

    NvEncLibraryHandle = FPlatformProcess::GetDllHandle(TEXT("nvEncodeAPI64.dll"));
    if (!NvEncLibraryHandle)
    {
        UE_LOG(LogPanoramaNVENC, Error, TEXT("Unable to load nvEncodeAPI64.dll."));
        return false;
    }

    FunctionList = new NV_ENCODE_API_FUNCTION_LIST();
    FMemory::Memzero(FunctionList, sizeof(NV_ENCODE_API_FUNCTION_LIST));
    FunctionList->version = NV_ENCODE_API_FUNCTION_LIST_VER;

    if (NVENCSTATUS Status = NvEncodeAPICreateInstance(FunctionList); Status != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaNVENC, Error, TEXT("NvEncodeAPICreateInstance failed (%d)."), static_cast<int32>(Status));
        return false;
    }

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS SessionParams = {};
    SessionParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    SessionParams.device = DeviceHandle;
    SessionParams.deviceType = bUsingD3D12 ? NV_ENC_DEVICE_TYPE_DIRECTX12 : NV_ENC_DEVICE_TYPE_DIRECTX;
    SessionParams.apiVersion = NVENCAPI_VERSION;

    if (NVENCSTATUS Status = FunctionList->nvEncOpenEncodeSessionEx(&SessionParams, &EncoderInterface); Status != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaNVENC, Error, TEXT("nvEncOpenEncodeSessionEx failed (%d)."), static_cast<int32>(Status));
        return false;
    }

    NV_ENC_CONFIG EncodeConfig = {};
    EncodeConfig.version = NV_ENC_CONFIG_VER;
    if (Config.OutputSettings.NVENC.Codec == ENVENCCodec::HEVC)
    {
        EncodeConfig.profileGUID = Config.bUse10Bit ? NV_ENC_HEVC_PROFILE_MAIN10_GUID : NV_ENC_HEVC_PROFILE_MAIN_GUID;
        EncodeConfig.encodeCodecConfig.hevcConfig.chromaFormatIDC = NV_ENC_CHROMA_FORMAT_YUV420;
        EncodeConfig.encodeCodecConfig.hevcConfig.pixelBitDepthMinus8 = Config.bUse10Bit ? 2 : 0;
    }
    else
    {
        EncodeConfig.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
        EncodeConfig.encodeCodecConfig.h264Config.chromaFormatIDC = NV_ENC_CHROMA_FORMAT_YUV420;
        EncodeConfig.encodeCodecConfig.h264Config.pixelBitDepthMinus8 = 0;
    }
    EncodeConfig.gopLength = Config.OutputSettings.NVENC.GOPLength;
    EncodeConfig.frameIntervalP = Config.OutputSettings.NVENC.bEnableBFrames ? Config.OutputSettings.NVENC.BFrameCount + 1 : 1;
    EncodeConfig.mvPrecision = NV_ENC_MV_PRECISION_QUARTER_PEL;

    NV_ENC_RC_PARAMS& RC = EncodeConfig.rcParams;
    RC.version = NV_ENC_RC_PARAMS_VER;
    RC.rateControlMode = GetRateControlMode(Config.OutputSettings.NVENC);
    RC.averageBitRate = static_cast<uint32>(Config.OutputSettings.NVENC.BitrateMbps * 1000000.0f);
    RC.maxBitRate = static_cast<uint32>(Config.OutputSettings.NVENC.MaxBitrateMbps * 1000000.0f);
    RC.vbvBufferSize = static_cast<uint32>(RC.averageBitRate * Config.OutputSettings.NVENC.VBVMultiplier);
    RC.vbvInitialDelay = RC.vbvBufferSize;
    RC.zeroReorderDelay = Config.OutputSettings.NVENC.bZeroLatency ? 1 : 0;
    RC.enableAQ = Config.OutputSettings.NVENC.bZeroLatency ? 0 : 1;

    NV_ENC_INITIALIZE_PARAMS InitParams = {};
    InitParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    InitParams.encodeGUID = GetCodecGuid(Config);
    InitParams.presetGUID = NV_ENC_PRESET_P3_GUID;
    InitParams.encodeWidth = Config.OutputResolution.X;
    InitParams.encodeHeight = Config.OutputResolution.Y;
    InitParams.darWidth = Config.OutputResolution.X;
    InitParams.darHeight = Config.OutputResolution.Y;
    InitParams.frameRateNum = Config.FrameRate;
    InitParams.frameRateDen = 1;
    InitParams.enablePTD = 1;
    InitParams.encodeConfig = &EncodeConfig;
    InitParams.bufferFormat = GetBufferFormat(Config);

    if (NVENCSTATUS Status = FunctionList->nvEncInitializeEncoder(EncoderInterface, &InitParams); Status != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaNVENC, Error, TEXT("nvEncInitializeEncoder failed (%d)."), static_cast<int32>(Status));
        return false;
    }

    const int32 BufferCount = FMath::Max(2, Config.OutputSettings.NVENC.AsyncDepth);
    AvailableBitstreams.Reserve(BufferCount);

    for (int32 Index = 0; Index < BufferCount; ++Index)
    {
        NV_ENC_CREATE_BITSTREAM_BUFFER BitstreamParams = {};
        BitstreamParams.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        if (NVENCSTATUS Status = FunctionList->nvEncCreateBitstreamBuffer(EncoderInterface, &BitstreamParams); Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogPanoramaNVENC, Error, TEXT("nvEncCreateBitstreamBuffer failed (%d)."), static_cast<int32>(Status));
            return false;
        }
        AvailableBitstreams.Add(BitstreamParams.bitstreamBuffer);
    }

    return true;
}

void FPanoramaNVENCEncoder::ShutdownSession()
{
    FlushPendingTasks();

    if (FunctionList && EncoderInterface)
    {
        for (TPair<FRHITexture*, FRegisteredResource>& Pair : RegisteredResources)
        {
            if (Pair.Value.RegisteredHandle)
            {
                FunctionList->nvEncUnregisterResource(EncoderInterface, Pair.Value.RegisteredHandle);
            }
        }
        RegisteredResources.Empty();

        for (void* Bitstream : AvailableBitstreams)
        {
            if (Bitstream)
            {
                FunctionList->nvEncDestroyBitstreamBuffer(EncoderInterface, Bitstream);
            }
        }
        AvailableBitstreams.Empty();

        FunctionList->nvEncDestroyEncoder(EncoderInterface);
        EncoderInterface = nullptr;
    }

    if (FunctionList)
    {
        delete FunctionList;
        FunctionList = nullptr;
    }

    if (NvEncLibraryHandle)
    {
        FPlatformProcess::FreeDllHandle(NvEncLibraryHandle);
        NvEncLibraryHandle = nullptr;
    }
}

bool FPanoramaNVENCEncoder::RegisterIfNeeded(FRHITexture* Texture, void** OutRegisteredHandle)
{
    if (FRegisteredResource* Existing = RegisteredResources.Find(Texture))
    {
        *OutRegisteredHandle = Existing->RegisteredHandle;
        return true;
    }

    FTextureRHIRef TextureRef = Texture;
    void* NativeResource = TextureRef->GetNativeResource();
    if (!NativeResource)
    {
        return false;
    }

    NV_ENC_REGISTER_RESOURCE RegisterParams = {};
    RegisterParams.version = NV_ENC_REGISTER_RESOURCE_VER;
    RegisterParams.resourceType = bUsingD3D12 ? NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX12 : NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    RegisterParams.resourceToRegister = NativeResource;
    RegisterParams.width = Config.OutputResolution.X;
    RegisterParams.height = Config.OutputResolution.Y;
    RegisterParams.bufferFormat = GetBufferFormat(Config);
    RegisterParams.bufferUsage = NV_ENC_INPUT_IMAGE;

    if (NVENCSTATUS Status = FunctionList->nvEncRegisterResource(EncoderInterface, &RegisterParams); Status != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaNVENC, Error, TEXT("nvEncRegisterResource failed (%d)."), static_cast<int32>(Status));
        return false;
    }

    FRegisteredResource Registered;
    Registered.Texture = Texture;
    Registered.RegisteredHandle = RegisterParams.registeredResource;
    RegisteredResources.Add(Texture, Registered);

    *OutRegisteredHandle = RegisterParams.registeredResource;
    return true;
}

void FPanoramaNVENCEncoder::EncodeSubmission(const FEncodeSubmission& Submission)
{
    FScopeLock Lock(&EncodeCriticalSection);

    if (!bInitialized)
    {
        return;
    }

    if (!Submission.Frame.RgbaTexture)
    {
        UE_LOG(LogPanoramaNVENC, Warning, TEXT("NVENC submission missing RGBA texture. Skipping frame."));
        ++Stats.DroppedFrames;
        return;
    }

    if ((Submission.Frame.bIsP010 ? 1 : 0) != (Config.bUse10Bit ? 1 : 0))
    {
        UE_LOG(LogPanoramaNVENC, Warning, TEXT("Encoder bit depth mismatch (frame %s, encoder %s). Dropping frame."), Submission.Frame.bIsP010 ? TEXT("P010") : TEXT("NV12"), Config.bUse10Bit ? TEXT("P010") : TEXT("NV12"));
        ++Stats.DroppedFrames;
        return;
    }

    const double SubmissionTime = Submission.Frame.TimeSeconds;
    FRHITexture* Texture = Submission.Frame.RgbaTexture;

    void* RegisteredHandle = nullptr;
    if (!RegisterIfNeeded(Texture, &RegisteredHandle))
    {
        ++Stats.DroppedFrames;
        return;
    }

    if (AvailableBitstreams.Num() == 0)
    {
        UE_LOG(LogPanoramaNVENC, Warning, TEXT("NVENC bitstream pool exhausted. Dropping frame."));
        ++Stats.DroppedFrames;
        return;
    }

    void* Bitstream = AvailableBitstreams.Pop(false);

    NV_ENC_MAP_INPUT_RESOURCE MapParams = {};
    MapParams.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    MapParams.registeredResource = RegisteredHandle;

    if (NVENCSTATUS Status = FunctionList->nvEncMapInputResource(EncoderInterface, &MapParams); Status != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaNVENC, Error, TEXT("nvEncMapInputResource failed (%d)."), static_cast<int32>(Status));
        AvailableBitstreams.Add(Bitstream);
        ++Stats.DroppedFrames;
        return;
    }

    NV_ENC_PIC_PARAMS PicParams = {};
    PicParams.version = NV_ENC_PIC_PARAMS_VER;
    PicParams.inputBuffer = MapParams.mappedResource;
    PicParams.bufferFmt = GetBufferFormat(Config);
    PicParams.inputWidth = Config.OutputResolution.X;
    PicParams.inputHeight = Config.OutputResolution.Y;
    PicParams.inputPitch = 0;
    PicParams.outputBitstream = Bitstream;
    PicParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    PicParams.inputTimeStamp = static_cast<uint64>(SubmissionTime * 1000.0);

    const double EncodeStart = FPlatformTime::Seconds();

    NVENCSTATUS EncodeStatus = FunctionList->nvEncEncodePicture(EncoderInterface, &PicParams);
    FunctionList->nvEncUnmapInputResource(EncoderInterface, MapParams.mappedResource);

    if (EncodeStatus != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaNVENC, Error, TEXT("nvEncEncodePicture failed (%d)."), static_cast<int32>(EncodeStatus));
        AvailableBitstreams.Add(Bitstream);
        ++Stats.DroppedFrames;
        return;
    }

    DrainBitstream(Bitstream, SubmissionTime);

    const double EncodeEnd = FPlatformTime::Seconds();
    TotalEncodeLatencySeconds += (EncodeEnd - EncodeStart);
    ++Stats.EncodedFrames;
}

void FPanoramaNVENCEncoder::DrainBitstream(void* OutputBitstream, double Timestamp)
{
    (void)Timestamp;
    NV_ENC_LOCK_BITSTREAM LockParams = {};
    LockParams.version = NV_ENC_LOCK_BITSTREAM_VER;
    LockParams.outputBitstream = OutputBitstream;
    LockParams.doNotWait = 0;

    if (NVENCSTATUS Status = FunctionList->nvEncLockBitstream(EncoderInterface, &LockParams); Status == NV_ENC_SUCCESS)
    {
        if (ElementaryStreamWriter && LockParams.bitstreamSizeInBytes > 0)
        {
            ElementaryStreamWriter->Serialize(LockParams.bitstreamBufferPtr, LockParams.bitstreamSizeInBytes);
        }
        FunctionList->nvEncUnlockBitstream(EncoderInterface, OutputBitstream);
    }
    else
    {
        UE_LOG(LogPanoramaNVENC, Warning, TEXT("nvEncLockBitstream failed (%d)."), static_cast<int32>(Status));
    }

    AvailableBitstreams.Add(OutputBitstream);
}

void FPanoramaNVENCEncoder::FlushPendingTasks()
{
    if (LastEncodeTask.IsValid())
    {
        FTaskGraphInterface::Get().WaitUntilTaskCompletes(LastEncodeTask);
        LastEncodeTask = nullptr;
    }
}

#endif // PLATFORM_WINDOWS && WITH_PANORAMA_NVENC

