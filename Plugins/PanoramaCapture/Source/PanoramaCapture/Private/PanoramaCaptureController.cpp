#include "PanoramaCaptureController.h"

#include "Async/Async.h"
#include "AudioMixerBlueprintLibrary.h"
#include "CaptureOutputSettings.h"
#include "CubemapCaptureRigComponent.h"
#include "CubemapEquirectPass.h"
#include "Containers/StringBuilder.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformProcess.h"
#include "Components/TextRenderComponent.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PanoramaCaptureModule.h"
#include "ComputeShaderUtils.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterUtils.h"
#include "Sound/SoundSubmixBase.h"
#include "Sound/SoundWave.h"
#include "TimerManager.h"
#include "TextureResource.h"
#include "UObject/Package.h"

namespace
{
    constexpr int32 FacesPerEye = 6;

    FString SanitizeFileComponent(const FString& Input)
    {
        FString Result = Input;
        static const TCHAR* InvalidCharacters[] = { TEXT("<"), TEXT(">"), TEXT(":"), TEXT("\""), TEXT("/"), TEXT("\\"), TEXT("|"), TEXT("?"), TEXT("*") };
        for (const TCHAR* InvalidCharacter : InvalidCharacters)
        {
            Result.ReplaceInline(InvalidCharacter, TEXT("_"));
        }
        Result.TrimStartAndEndInline();
        if (Result.IsEmpty())
        {
            Result = TEXT("PanoramaCapture");
        }
        return Result;
    }

#if WITH_PANORAMA_NVENC
    class FEncodeSurfaceCS : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FEncodeSurfaceCS);
        SHADER_USE_PARAMETER_STRUCT(FEncodeSurfaceCS, FGlobalShader);

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
#if PLATFORM_WINDOWS
            return Parameters.Platform == SP_PCD3D_SM5;
#else
            return false;
#endif
        }

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceTexture)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
            SHADER_PARAMETER(uint32, bApplySRGB)
        END_SHADER_PARAMETER_STRUCT()
    };
#endif // WITH_PANORAMA_NVENC

class FPendingCapturePayload : public TSharedFromThis<FPendingCapturePayload, ESPMode::ThreadSafe>
    {
    public:
        FPendingCapturePayload(const FCaptureOutputSettings& InSettings, FIntPoint InResolution, double InTimeSeconds, int32 InFrameIndex, const FString& InOutputFile, bool bInPreviewOnly)
            : Settings(InSettings)
            , Resolution(InResolution)
            , TimeSeconds(InTimeSeconds)
            , FrameIndex(InFrameIndex)
            , OutputFile(InOutputFile)
            , Readback(MakeUnique<FRHIGPUTextureReadback>(TEXT("PanoramaCaptureReadback")))
            , bPreviewOnly(bInPreviewOnly)
        {
        }

        FRHIGPUTextureReadback* GetReadback() const
        {
            return Readback.Get();
        }

        bool IsReady() const
        {
            return Readback && Readback->IsReady();
        }

        FPanoramaCaptureFrame Resolve()
        {
            check(IsReady());

            const int32 Width = Resolution.X;
            const int32 Height = Resolution.Y;
            const bool bUse16BitPNG = Settings.bUse16BitPNG;

            TArray<uint8> Payload;
            Payload.SetNumUninitialized(Width * Height * (bUse16BitPNG ? sizeof(uint16) * 4 : 4));

            int32 RowPitch = 0;
            const uint8* SourceData = static_cast<const uint8*>(Readback->Lock(RowPitch));

            if (bUse16BitPNG)
            {
                uint16* DestData = reinterpret_cast<uint16*>(Payload.GetData());
                for (int32 Y = 0; Y < Height; ++Y)
                {
                    const float* SrcRow = reinterpret_cast<const float*>(SourceData + Y * RowPitch);
                    for (int32 X = 0; X < Width; ++X)
                    {
                        const int32 SrcIndex = X * 4;
                        const int32 DestIndex = (Y * Width + X) * 4;
                        DestData[DestIndex + 0] = (uint16)FMath::Clamp<int32>(FMath::RoundToInt(SrcRow[SrcIndex + 0] * 65535.f), 0, 65535);
                        DestData[DestIndex + 1] = (uint16)FMath::Clamp<int32>(FMath::RoundToInt(SrcRow[SrcIndex + 1] * 65535.f), 0, 65535);
                        DestData[DestIndex + 2] = (uint16)FMath::Clamp<int32>(FMath::RoundToInt(SrcRow[SrcIndex + 2] * 65535.f), 0, 65535);
                        DestData[DestIndex + 3] = (uint16)FMath::Clamp<int32>(FMath::RoundToInt(SrcRow[SrcIndex + 3] * 65535.f), 0, 65535);
                    }
                }
            }
            else
            {
                uint8* DestData = Payload.GetData();
                for (int32 Y = 0; Y < Height; ++Y)
                {
                    const float* SrcRow = reinterpret_cast<const float*>(SourceData + Y * RowPitch);
                    for (int32 X = 0; X < Width; ++X)
                    {
                        const int32 SrcIndex = X * 4;
                        const int32 DestIndex = (Y * Width + X) * 4;
                        DestData[DestIndex + 0] = (uint8)FMath::Clamp<int32>(FMath::RoundToInt(SrcRow[SrcIndex + 0] * 255.f), 0, 255);
                        DestData[DestIndex + 1] = (uint8)FMath::Clamp<int32>(FMath::RoundToInt(SrcRow[SrcIndex + 1] * 255.f), 0, 255);
                        DestData[DestIndex + 2] = (uint8)FMath::Clamp<int32>(FMath::RoundToInt(SrcRow[SrcIndex + 2] * 255.f), 0, 255);
                        DestData[DestIndex + 3] = (uint8)FMath::Clamp<int32>(FMath::RoundToInt(SrcRow[SrcIndex + 3] * 255.f), 0, 255);
                    }
                }
            }

            Readback->Unlock();
            Readback.Reset();

            return FPanoramaCaptureFrame(Resolution, TimeSeconds, FrameIndex, OutputFile, bUse16BitPNG, MoveTemp(Payload));
        }

        bool IsPreviewOnly() const
        {
            return bPreviewOnly;
        }

    private:
        FCaptureOutputSettings Settings;
        FIntPoint Resolution;
        double TimeSeconds;
        int32 FrameIndex;
        FString OutputFile;
        TUniquePtr<FRHIGPUTextureReadback> Readback;
        bool bPreviewOnly;
    };
}

#if WITH_PANORAMA_NVENC
IMPLEMENT_GLOBAL_SHADER(FEncodeSurfaceCS, "/PanoramaCapture/Private/EncodeSurface.usf", "MainCS", SF_Compute);
#endif

UPanoramaCaptureController::UPanoramaCaptureController()
    : bIsCapturing(false)
    , CaptureStartSeconds(0.0)
    , CaptureFrameCounter(0)
    , CurrentStatus(TEXT("Idle"))
    , LastStatusUpdateSeconds(0.0)
    , AudioCaptureStartSeconds(0.0)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UPanoramaCaptureController::BeginPlay()
{
    Super::BeginPlay();
    EnsureRig();
    EnsureStatusDisplay();
}

void UPanoramaCaptureController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopCapture();
    Super::EndPlay(EndPlayReason);
}

void UPanoramaCaptureController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bIsCapturing || PendingReadbacks.Num() > 0)
    {
        ConsumeFrameQueue();
    }
}

void UPanoramaCaptureController::StartCapture()
{
    if (bIsCapturing)
    {
        return;
    }

    EnsureRig();
    EnsureStatusDisplay();

    if (!ManagedRig)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Cannot start capture without a cubemap rig component."));
        return;
    }

    FrameBuffer.Clear();
    PendingReadbacks.Reset();
    CapturedFrameFiles.Reset();
    CapturedFrameTimes.Reset();
    FirstVideoTimestamp.Reset();
    LastVideoTimestamp.Reset();
    AudioCaptureStartSeconds = 0.0;
    RecordedAudioFile.Reset();
    PendingWriteTasks.Reset();
    ActiveElementaryStream.Reset();

    InitializeOutputDirectory();
    InitializeRingBuffer();

    CaptureFrameCounter = 0;
    LastStatusUpdateSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    CaptureStartSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : FPlatformTime::Seconds();

#if WITH_PANORAMA_NVENC
    if (OutputSettings.OutputPath == ECaptureOutputPath::NVENCVideo)
    {
        ActiveEncoder = FPanoramaCaptureModule::Get().CreateVideoEncoder();
        if (!ActiveEncoder.IsValid())
        {
            UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC module not available. Falling back to PNG sequence output."));
            OutputSettings.OutputPath = ECaptureOutputPath::PNGSequence;
        }
        else
        {
            const FIntPoint OutputResolution(OutputSettings.Resolution.Width, OutputSettings.Resolution.Height);
            const FString ElementaryExtension = (OutputSettings.NVENC.Codec == ENVENCCodec::HEVC) ? TEXT("h265") : TEXT("h264");
            ActiveElementaryStream = BuildVideoFilePath(ElementaryExtension);

            FPanoramaVideoEncoderConfig EncoderConfig;
            EncoderConfig.OutputSettings = OutputSettings;
            EncoderConfig.OutputFile = BuildVideoFilePath(OutputSettings.ContainerFormat);
            EncoderConfig.ElementaryStreamFile = ActiveElementaryStream;
            EncoderConfig.OutputResolution = OutputResolution;
            EncoderConfig.FrameRate = FMath::Max(1, OutputSettings.FrameRate);
            EncoderConfig.bUseD3D12 = OutputSettings.bPreferD3D12Interop;
            EncoderConfig.bUse10Bit = OutputSettings.NVENC.bUseP010;

            if (!ActiveEncoder->Initialize(EncoderConfig))
            {
                UE_LOG(LogPanoramaCapture, Error, TEXT("Failed to initialize NVENC encoder. Falling back to PNG sequence output."));
                ActiveElementaryStream.Reset();
                ActiveEncoder.Reset();
                OutputSettings.OutputPath = ECaptureOutputPath::PNGSequence;
            }
        }
    }
#else
    if (OutputSettings.OutputPath == ECaptureOutputPath::NVENCVideo)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC output requested on unsupported platform. Using PNG sequence instead."));
        OutputSettings.OutputPath = ECaptureOutputPath::PNGSequence;
    }
#endif

    if (OutputSettings.bRecordAudio)
    {
        InitializeAudioCapture();
    }

    ManagedRig->OutputSettings = OutputSettings;
    ManagedRig->bStereo = (OutputSettings.StereoMode != EPanoramaStereoMode::Mono);
    ManagedRig->InitializeRig();

    const float Interval = 1.0f / FMath::Max(1, OutputSettings.FrameRate);
    bIsCapturing = true;
    UpdateStatus(TEXT("Recording"));

    GetWorld()->GetTimerManager().SetTimer(CaptureTimerHandle, this, &UPanoramaCaptureController::CaptureFrame, Interval, true);
}

void UPanoramaCaptureController::StopCapture()
{
    if (!bIsCapturing)
    {
        return;
    }

    bIsCapturing = false;
    GetWorld()->GetTimerManager().ClearTimer(CaptureTimerHandle);

    ShutdownAudioCapture();

    FlushRenderingCommands();

    const double WaitStart = FPlatformTime::Seconds();
    while (PendingReadbacks.Num() > 0)
    {
        ProcessPendingReadbacks();
        ConsumeFrameQueue();

        if (PendingReadbacks.Num() == 0)
        {
            break;
        }

        if (FPlatformTime::Seconds() - WaitStart > 5.0)
        {
            UE_LOG(LogPanoramaCapture, Warning, TEXT("Timed out waiting for GPU readbacks during capture shutdown."));
            break;
        }

        FPlatformProcess::Sleep(0.001f);
    }

    ConsumeFrameQueue();

    if (ActiveEncoder.IsValid())
    {
        ActiveEncoder->Flush();
        FString ElementaryStreamPath;
        if (ActiveEncoder->FinalizeEncoding(ElementaryStreamPath))
        {
            ActiveElementaryStream = ElementaryStreamPath;
        }
        ActiveEncoder.Reset();
    }

    FinalizeCaptureOutputs();

    PendingReadbacks.Reset();

    UpdateStatus(TEXT("Idle"));
}

void UPanoramaCaptureController::EnsureRig()
{
    if (ManagedRig)
    {
        return;
    }

    if (AActor* OwnerActor = GetOwner())
    {
        ManagedRig = OwnerActor->FindComponentByClass<UCubemapCaptureRigComponent>();
        if (!ManagedRig)
        {
            ManagedRig = NewObject<UCubemapCaptureRigComponent>(OwnerActor, TEXT("PanoramaCaptureRig"));
            ManagedRig->RegisterComponent();
        }
    }

    if (ManagedRig)
    {
        ManagedRig->OutputSettings = OutputSettings;
        ManagedRig->bStereo = (OutputSettings.StereoMode != EPanoramaStereoMode::Mono);
    }
}

void UPanoramaCaptureController::EnsureStatusDisplay()
{
    if (StatusBillboard || !GetOwner())
    {
        return;
    }

    StatusBillboard = NewObject<UTextRenderComponent>(GetOwner(), TEXT("PanoramaCaptureStatus"));
    if (!StatusBillboard)
    {
        return;
    }

    if (USceneComponent* Root = GetOwner()->GetRootComponent())
    {
        StatusBillboard->SetupAttachment(Root);
    }

    StatusBillboard->RegisterComponent();
    StatusBillboard->SetHorizontalAlignment(EHTA_Center);
    StatusBillboard->SetVerticalAlignment(EVRTA_TextCenter);
    StatusBillboard->SetWorldSize(48.f);
    StatusBillboard->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
    StatusBillboard->SetTextRenderColor(FColor::White);
    StatusBillboard->SetText(FText::FromString(TEXT("Idle")));
}

void UPanoramaCaptureController::CaptureFrame()
{
    if (!ManagedRig)
    {
        return;
    }

    ManagedRig->TickRig(0.0f);

    const double Now = GetWorld()->GetTimeSeconds() - CaptureStartSeconds;
    const FIntPoint OutputResolution(OutputSettings.Resolution.Width, OutputSettings.Resolution.Height);
    const bool bStereo = (OutputSettings.StereoMode != EPanoramaStereoMode::Mono);
    const bool bOverUnder = (OutputSettings.StereoMode == EPanoramaStereoMode::StereoOverUnder);
    const bool bLinearGamma = (OutputSettings.GammaSpace == EPanoramaGammaSpace::Linear);

    const bool bNeedsReadback = (OutputSettings.OutputPath == ECaptureOutputPath::PNGSequence) || OutputSettings.bEnablePreview;
    FString FrameOutputFile;
    TSharedPtr<FPendingCapturePayload, ESPMode::ThreadSafe> PendingPayload;
    if (bNeedsReadback)
    {
        FCaptureOutputSettings PayloadSettings = OutputSettings;
        bool bPreviewOnly = false;

        if (OutputSettings.OutputPath == ECaptureOutputPath::PNGSequence)
        {
            FrameOutputFile = BuildFrameFilePath(CaptureFrameCounter);
        }
        else
        {
            bPreviewOnly = true;
            PayloadSettings.bUse16BitPNG = false;
        }

        PendingPayload = MakeShared<FPendingCapturePayload, ESPMode::ThreadSafe>(PayloadSettings, OutputResolution, Now, CaptureFrameCounter, FrameOutputFile, bPreviewOnly);
        PendingReadbacks.Add(PendingPayload);
    }

    if (!FirstVideoTimestamp.IsSet())
    {
        FirstVideoTimestamp = Now;
    }
    LastVideoTimestamp = Now;
    ++CaptureFrameCounter;

    const int32 EyeCount = bStereo ? 2 : 1;
    TArray<FTextureRenderTargetResource*, TInlineAllocator<FacesPerEye * 2>> FaceResources;
    FaceResources.Reserve(EyeCount * FacesPerEye);

    for (int32 EyeIndex = 0; EyeIndex < EyeCount; ++EyeIndex)
    {
        const bool bLeftEye = (EyeIndex == 0);
        for (int32 FaceIndex = 0; FaceIndex < FacesPerEye; ++FaceIndex)
        {
            if (UTextureRenderTarget2D* Target = ManagedRig->GetFaceRenderTarget(FaceIndex, bLeftEye))
            {
                if (FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource())
                {
                    FaceResources.Add(Resource);
                }
            }
        }
    }

    if (FaceResources.Num() == 0)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("No cubemap faces available for capture."));
        return;
    }

    const FCaptureOutputSettings LocalSettings = OutputSettings;
    TWeakPtr<IPanoramaVideoEncoder, ESPMode::ThreadSafe> EncoderWeak = ActiveEncoder;

    ENQUEUE_RENDER_COMMAND(PanoramaCaptureSubmit)(
        [FaceResources, PendingPayload, OutputResolution, bStereo, bOverUnder, bLinearGamma, LocalSettings, EncoderWeak, Now](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);

            TArray<FRDGTextureRef, TInlineAllocator<FacesPerEye * 2>> RegisteredFaces;
            RegisteredFaces.Reserve(FaceResources.Num());

            for (int32 Index = 0; Index < FaceResources.Num(); ++Index)
            {
                if (FTextureRenderTargetResource* Resource = FaceResources[Index])
                {
                    const FTextureRHIRef TextureRHI = Resource->GetRenderTargetTexture();
                    if (TextureRHI.IsValid())
                    {
                        const FString DebugName = FString::Printf(TEXT("PanoramaFace_%d"), Index);
                        FRDGTextureRef Registered = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(TextureRHI, *DebugName));
                        RegisteredFaces.Add(Registered);
                    }
                }
            }

            if (RegisteredFaces.Num() == 0)
            {
                return;
            }

            const FIntPoint FaceSize(FaceResources[0]->GetSizeX(), FaceResources[0]->GetSizeY());
            const EPixelFormat FaceFormat = FaceResources[0]->GetTextureRHI()->GetFormat();

            FRDGTextureDesc CubeDesc = FRDGTextureDesc::CreateCube(FaceSize.X, FaceFormat, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV);
            FRDGTextureRef LeftCube = GraphBuilder.CreateTexture(CubeDesc, TEXT("PanoramaCubeLeft"));
            FRDGTextureRef RightCube = bStereo ? GraphBuilder.CreateTexture(CubeDesc, TEXT("PanoramaCubeRight")) : LeftCube;

            for (int32 FaceIndex = 0; FaceIndex < FacesPerEye && FaceIndex < RegisteredFaces.Num(); ++FaceIndex)
            {
                FRHICopyTextureInfo CopyInfo;
                CopyInfo.DestSliceIndex = FaceIndex;
                AddCopyTexturePass(GraphBuilder, RegisteredFaces[FaceIndex], LeftCube, CopyInfo);
            }

            if (bStereo)
            {
                for (int32 FaceIndex = 0; FaceIndex < FacesPerEye; ++FaceIndex)
                {
                    const int32 SourceIndex = FaceIndex + FacesPerEye;
                    if (RegisteredFaces.IsValidIndex(SourceIndex))
                    {
                        FRHICopyTextureInfo CopyInfo;
                        CopyInfo.DestSliceIndex = FaceIndex;
                        AddCopyTexturePass(GraphBuilder, RegisteredFaces[SourceIndex], RightCube, CopyInfo);
                    }
                }
            }

            FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(OutputResolution.X, OutputResolution.Y, PF_FloatRGBA, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV);
            FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("PanoramaEquirect"));

            FCubemapEquirectDispatchParams DispatchParams;
            DispatchParams.SourceCubemapLeft = LeftCube;
            DispatchParams.SourceCubemapRight = RightCube;
            DispatchParams.DestinationEquirect = OutputTexture;
            DispatchParams.OutputResolution = OutputResolution;
            DispatchParams.bStereo = bStereo;
            DispatchParams.bLinearGamma = bLinearGamma;
            DispatchParams.bStereoOverUnder = bOverUnder;

            FCubemapEquirectPass::AddComputePass(GraphBuilder, DispatchParams);

            const bool bEncodeNVENC =
#if WITH_PANORAMA_NVENC
                (LocalSettings.OutputPath == ECaptureOutputPath::NVENCVideo);
#else
                false;
#endif
            FRDGTextureRef NVENCEncodeTexture = nullptr;

#if WITH_PANORAMA_NVENC
            if (bEncodeNVENC)
            {
                const EPixelFormat EncodeFormat = LocalSettings.NVENC.bUseP010 ? PF_A2B10G10R10 : PF_B8G8R8A8;
                FRDGTextureDesc EncodeDesc = FRDGTextureDesc::Create2D(OutputResolution.X, OutputResolution.Y, EncodeFormat, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV);
                NVENCEncodeTexture = GraphBuilder.CreateTexture(EncodeDesc, TEXT("PanoramaNVENCEncode"));

                FEncodeSurfaceCS::FParameters* EncodeParameters = GraphBuilder.AllocParameters<FEncodeSurfaceCS::FParameters>();
                EncodeParameters->SourceTexture = OutputTexture;
                EncodeParameters->OutputTexture = GraphBuilder.CreateUAV(NVENCEncodeTexture);
                EncodeParameters->bApplySRGB = bLinearGamma ? 1u : 0u;

                TShaderMapRef<FEncodeSurfaceCS> EncodeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                const FIntVector EncodeGroupCount(
                    FMath::DivideAndRoundUp(OutputResolution.X, 8),
                    FMath::DivideAndRoundUp(OutputResolution.Y, 8),
                    1);

                FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Panorama::EncodeSurface"), EncodeShader, EncodeParameters, EncodeGroupCount);
            }
#endif // WITH_PANORAMA_NVENC

            if (PendingPayload.IsValid())
            {
                if (FRHIGPUTextureReadback* Readback = PendingPayload->GetReadback())
                {
                    AddEnqueueCopyPass(GraphBuilder, Readback, OutputTexture, FIntRect(0, 0, OutputResolution.X, OutputResolution.Y));
                }
            }

            TRefCountPtr<IPooledRenderTarget> ExtractedTexture;
            GraphBuilder.QueueTextureExtraction(OutputTexture, &ExtractedTexture);

            TRefCountPtr<IPooledRenderTarget> ExtractedNVENCTexture;
            if (NVENCEncodeTexture)
            {
                GraphBuilder.QueueTextureExtraction(NVENCEncodeTexture, &ExtractedNVENCTexture);
            }

            GraphBuilder.Execute();

#if WITH_PANORAMA_NVENC
            if (bEncodeNVENC)
            {
                if (TSharedPtr<IPanoramaVideoEncoder> Encoder = EncoderWeak.Pin())
                {
                    if (ExtractedNVENCTexture.IsValid())
                    {
                        FPanoramaVideoEncoderFrame EncoderFrame;
                        EncoderFrame.RgbaTexture = ExtractedNVENCTexture->GetRHI();
                        EncoderFrame.TimeSeconds = Now;
                        Encoder->EncodeFrame(EncoderFrame);
                    }
                }
            }
#endif
        });
}

void UPanoramaCaptureController::ConsumeFrameQueue()
{
    ProcessPendingReadbacks();

    FPanoramaCaptureFrame Frame;
    while (FrameBuffer.Dequeue(Frame))
    {
        if (OutputSettings.OutputPath == ECaptureOutputPath::PNGSequence)
        {
            WritePNGFrame(MoveTemp(Frame));
        }
    }
}

void UPanoramaCaptureController::ProcessPendingReadbacks()
{
    for (int32 Index = PendingReadbacks.Num() - 1; Index >= 0; --Index)
    {
        const TSharedPtr<FPendingCapturePayload, ESPMode::ThreadSafe>& Pending = PendingReadbacks[Index];
        if (Pending.IsValid() && Pending->IsReady())
        {
            const bool bPreviewOnly = Pending->IsPreviewOnly();
            FPanoramaCaptureFrame ResolvedFrame = Pending->Resolve();

            if (OutputSettings.bEnablePreview)
            {
                UpdatePreviewFromFrame(ResolvedFrame);
            }

            if (!bPreviewOnly)
            {
                if (!FrameBuffer.Enqueue(MoveTemp(ResolvedFrame)))
                {
                    UpdateStatus(TEXT("Dropped"));
                }
                else if (bIsCapturing)
                {
                    UpdateStatus(TEXT("Recording"));
                }
            }
            PendingReadbacks.RemoveAtSwap(Index);
        }
    }
}

void UPanoramaCaptureController::WritePNGFrame(FPanoramaCaptureFrame&& Frame)
{
    const FString OutputFile = Frame.OutputFile;
    CapturedFrameFiles.Add(OutputFile);
    const double FrameTimeSeconds = Frame.TimeSeconds;

    const bool bUse16BitPNG = Frame.bIs16Bit;
    const FIntPoint Resolution = Frame.Resolution;

    TArray<uint8> Payload = MoveTemp(Frame.Payload);

    TFuture<void> WriteTask = Async(EAsyncExecution::ThreadPool,
        [Payload = MoveTemp(Payload), Resolution, OutputFile, bUse16BitPNG]()
        {
            if (Payload.Num() == 0)
            {
                return;
            }

            IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
            TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
            const ERGBFormat RGBFormat = ERGBFormat::RGBA;
            const int32 BitDepth = bUse16BitPNG ? 16 : 8;

            if (Wrapper->SetRaw(Payload.GetData(), Payload.Num(), Resolution.X, Resolution.Y, RGBFormat, BitDepth))
            {
                const TArray64<uint8>& Compressed = Wrapper->GetCompressed(0);
                FFileHelper::SaveArrayToFile(Compressed, *OutputFile);
            }
        });
    PendingWriteTasks.Add(MoveTemp(WriteTask));
    CapturedFrameTimes.Add(FrameTimeSeconds);
}

void UPanoramaCaptureController::UpdatePreviewFromFrame(const FPanoramaCaptureFrame& Frame)
{
    if (!OutputSettings.bEnablePreview)
    {
        return;
    }

    const int32 SourceWidth = Frame.Resolution.X;
    const int32 SourceHeight = Frame.Resolution.Y;
    const float PreviewScale = FMath::Clamp(OutputSettings.PreviewScale, 0.1f, 1.0f);
    const int32 PreviewWidth = FMath::Max(1, FMath::RoundToInt(SourceWidth * PreviewScale));
    const int32 PreviewHeight = FMath::Max(1, FMath::RoundToInt(SourceHeight * PreviewScale));

    TArray<uint8> PreviewPixels;
    PreviewPixels.SetNumUninitialized(PreviewWidth * PreviewHeight * 4);

    const float StepX = static_cast<float>(SourceWidth) / static_cast<float>(PreviewWidth);
    const float StepY = static_cast<float>(SourceHeight) / static_cast<float>(PreviewHeight);

    if (Frame.bIs16Bit)
    {
        const uint16* SourceData = reinterpret_cast<const uint16*>(Frame.Payload.GetData());
        for (int32 Y = 0; Y < PreviewHeight; ++Y)
        {
            const int32 SrcY = FMath::Clamp(static_cast<int32>(Y * StepY), 0, SourceHeight - 1);
            for (int32 X = 0; X < PreviewWidth; ++X)
            {
                const int32 SrcX = FMath::Clamp(static_cast<int32>(X * StepX), 0, SourceWidth - 1);
                const int32 SrcIndex = (SrcY * SourceWidth + SrcX) * 4;
                const int32 DstIndex = (Y * PreviewWidth + X) * 4;
                PreviewPixels[DstIndex + 0] = static_cast<uint8>(SourceData[SrcIndex + 2] >> 8);
                PreviewPixels[DstIndex + 1] = static_cast<uint8>(SourceData[SrcIndex + 1] >> 8);
                PreviewPixels[DstIndex + 2] = static_cast<uint8>(SourceData[SrcIndex + 0] >> 8);
                PreviewPixels[DstIndex + 3] = static_cast<uint8>(SourceData[SrcIndex + 3] >> 8);
            }
        }
    }
    else
    {
        const uint8* SourceData = Frame.Payload.GetData();
        for (int32 Y = 0; Y < PreviewHeight; ++Y)
        {
            const int32 SrcY = FMath::Clamp(static_cast<int32>(Y * StepY), 0, SourceHeight - 1);
            for (int32 X = 0; X < PreviewWidth; ++X)
            {
                const int32 SrcX = FMath::Clamp(static_cast<int32>(X * StepX), 0, SourceWidth - 1);
                const int32 SrcIndex = (SrcY * SourceWidth + SrcX) * 4;
                const int32 DstIndex = (Y * PreviewWidth + X) * 4;
                PreviewPixels[DstIndex + 0] = SourceData[SrcIndex + 2];
                PreviewPixels[DstIndex + 1] = SourceData[SrcIndex + 1];
                PreviewPixels[DstIndex + 2] = SourceData[SrcIndex + 0];
                PreviewPixels[DstIndex + 3] = SourceData[SrcIndex + 3];
            }
        }
    }

    if (!PreviewTexture || PreviewTexture->GetSizeX() != PreviewWidth || PreviewTexture->GetSizeY() != PreviewHeight)
    {
        PreviewTexture = UTexture2D::CreateTransient(PreviewWidth, PreviewHeight, PF_B8G8R8A8);
        PreviewTexture->SRGB = true;
    }

    if (PreviewTexture && PreviewTexture->GetPlatformData() && PreviewTexture->GetPlatformData()->Mips.Num() > 0)
    {
        FTexture2DMipMap& Mip = PreviewTexture->GetPlatformData()->Mips[0];
        void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(TextureData, PreviewPixels.GetData(), PreviewPixels.Num());
        Mip.BulkData.Unlock();
        PreviewTexture->UpdateResource();
    }
}

void UPanoramaCaptureController::UpdateStatus(FName NewStatus)
{
    const double NowSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : FPlatformTime::Seconds();
    FString StatusLabel = NewStatus.ToString();

    const int32 QueueCount = FrameBuffer.Num();
    const int32 DroppedCount = FrameBuffer.GetDroppedFrames();
    const int32 BlockedCount = FrameBuffer.GetBlockedFrames();

    StatusLabel += FString::Printf(TEXT("|Q:%d"), QueueCount);

    if (DroppedCount > 0)
    {
        StatusLabel += FString::Printf(TEXT("|Drop:%d"), DroppedCount);
    }

    if (BlockedCount > 0)
    {
        StatusLabel += FString::Printf(TEXT("|Block:%d"), BlockedCount);
    }

    if (ActiveEncoder.IsValid())
    {
        const FPanoramaVideoEncoderStats EncoderStats = ActiveEncoder->GetStats();
        if (EncoderStats.QueuedFrames > 0)
        {
            StatusLabel += FString::Printf(TEXT("|EncQ:%d"), EncoderStats.QueuedFrames);
        }
        if (EncoderStats.DroppedFrames > 0)
        {
            StatusLabel += FString::Printf(TEXT("|EncDrop:%d"), EncoderStats.DroppedFrames);
        }
    }

    const FName EnrichedStatus(*StatusLabel);

    const bool bStatusChanged = (CurrentStatus != EnrichedStatus);
    const bool bTimeElapsed = (NowSeconds - LastStatusUpdateSeconds) > 0.5;

    if (bStatusChanged || bTimeElapsed)
    {
        CurrentStatus = EnrichedStatus;
        LastStatusUpdateSeconds = NowSeconds;
        OnStatusChanged.Broadcast(EnrichedStatus);

        UE_LOG(LogPanoramaCapture, Log, TEXT("Capture status updated: %s"), *StatusLabel);
    }

    if (StatusBillboard)
    {
        StatusBillboard->SetText(FText::FromString(StatusLabel));

        FColor StatusColor = FColor::White;
        if (NewStatus == TEXT("Recording"))
        {
            StatusColor = FColor::Green;
        }
        else if (NewStatus == TEXT("Dropped"))
        {
            StatusColor = FColor::Orange;
        }
        else if (NewStatus == TEXT("Idle"))
        {
            StatusColor = FColor::Silver;
        }
        StatusBillboard->SetTextRenderColor(StatusColor);
    }
}

void UPanoramaCaptureController::InitializeRingBuffer()
{
    int32 TargetCapacity = OutputSettings.RingBufferCapacityOverride;

    if (!OutputSettings.bUseRingBuffer)
    {
        TargetCapacity = 1;
    }
    else if (TargetCapacity <= 0)
    {
        const float Duration = FMath::Max(0.1f, OutputSettings.RingBufferDurationSeconds);
        TargetCapacity = FMath::Max(1, FMath::RoundToInt(OutputSettings.FrameRate * Duration));
    }

    FrameBuffer.Initialize(TargetCapacity, OutputSettings.RingBufferPolicy);
}

void UPanoramaCaptureController::InitializeOutputDirectory()
{
    const UPanoramaCaptureSettings* Settings = GetDefault<UPanoramaCaptureSettings>();

    FString DirectorySetting = OutputSettings.OutputDirectory;
    if (DirectorySetting.IsEmpty())
    {
        DirectorySetting = Settings->DefaultOutputDirectory;
    }

    FString RootDirectory;
    if (!DirectorySetting.IsEmpty() && !FPaths::IsRelative(DirectorySetting))
    {
        RootDirectory = DirectorySetting;
    }
    else
    {
        const FString SanitizedDirectory = SanitizeFileComponent(DirectorySetting);
        if (SanitizedDirectory.IsEmpty())
        {
            RootDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PanoramaCapture"));
        }
        else
        {
            RootDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), SanitizedDirectory);
        }
    }

    ActiveBaseFileName = OutputSettings.BaseFileName.IsEmpty() ? TEXT("PanoramaCapture") : SanitizeFileComponent(OutputSettings.BaseFileName);

    if (ActiveBaseFileName.IsEmpty())
    {
        ActiveBaseFileName = TEXT("PanoramaCapture");
    }

    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    ActiveCaptureDirectory = FPaths::Combine(RootDirectory, Timestamp);

    IFileManager::Get().MakeDirectory(*ActiveCaptureDirectory, true);
}

void UPanoramaCaptureController::InitializeAudioCapture()
{
#if WITH_AUDIO_MIXER
    const UPanoramaCaptureSettings* Settings = GetDefault<UPanoramaCaptureSettings>();
    RecordedSubmix.Reset();

    AudioCaptureStartSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : FPlatformTime::Seconds();

    if (Settings->AudioSubmix != NAME_None)
    {
        RecordedSubmix = FindObject<USoundSubmixBase>(ANY_PACKAGE, *Settings->AudioSubmix.ToString());
    }

    if (RecordedSubmix.IsValid())
    {
        UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 0.0f, RecordedSubmix.Get());
        UE_LOG(LogPanoramaCapture, Log, TEXT("Recording audio from submix '%s'"), *RecordedSubmix->GetName());
    }
    else if (Settings->AudioSubmix != NAME_None)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Audio submix '%s' not found. Audio will not be recorded."), *Settings->AudioSubmix.ToString());
    }
#else
    UE_LOG(LogPanoramaCapture, Warning, TEXT("AudioMixer module is not enabled. Audio will not be recorded."));
#endif
}

void UPanoramaCaptureController::ShutdownAudioCapture()
{
#if WITH_AUDIO_MIXER
    if (!RecordedSubmix.IsValid())
    {
        return;
    }

    if (USoundWave* RecordedWave = UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::SoundWave, ActiveBaseFileName, RecordedSubmix.Get()))
    {
        const FString AudioPath = BuildVideoFilePath(TEXT("wav"));
        UAudioMixerBlueprintLibrary::ExportToWavFile(RecordedWave, AudioPath);
        RecordedAudioFile = AudioPath;
        UE_LOG(LogPanoramaCapture, Log, TEXT("Wrote audio track to '%s'"), *AudioPath);
    }

    RecordedSubmix.Reset();
#else
    RecordedAudioFile.Reset();
#endif
}

void UPanoramaCaptureController::FinalizeCaptureOutputs()
{
    for (TFuture<void>& Task : PendingWriteTasks)
    {
        Task.Wait();
    }
    PendingWriteTasks.Reset();

    if (OutputSettings.OutputPath == ECaptureOutputPath::PNGSequence)
    {
        if (!OutputSettings.bAutoAssembleVideo)
        {
            return;
        }

        if (CapturedFrameFiles.Num() == 0)
        {
            UE_LOG(LogPanoramaCapture, Warning, TEXT("No PNG frames were written. Skipping video assembly."));
            return;
        }

        const FString Extension = OutputSettings.ContainerFormat.IsEmpty() ? TEXT("mp4") : OutputSettings.ContainerFormat;
        const FString OutputVideo = BuildVideoFilePath(Extension);

        TStringBuilder<4096> ConcatBuilder;
        ConcatBuilder.Append(TEXT("ffconcat version 1.0\n"));

        const int32 FrameCount = CapturedFrameFiles.Num();
        const int32 TimeCount = CapturedFrameTimes.Num();
        const double DefaultDuration = 1.0 / static_cast<double>(FMath::Max(1, OutputSettings.FrameRate));

        for (int32 Index = 0; Index < FrameCount; ++Index)
        {
            const FString AbsolutePath = FPaths::ConvertRelativePathToFull(CapturedFrameFiles[Index]);
            ConcatBuilder.Appendf(TEXT("file '%s'\n"), *AbsolutePath);

            if (Index < TimeCount - 1)
            {
                double Duration = CapturedFrameTimes[Index + 1] - CapturedFrameTimes[Index];
                Duration = FMath::Max(Duration, DefaultDuration * 0.25);
                ConcatBuilder.Appendf(TEXT("duration %.6f\n"), Duration);
            }
        }

        if (FrameCount > 0)
        {
            const FString AbsolutePath = FPaths::ConvertRelativePathToFull(CapturedFrameFiles.Last());
            ConcatBuilder.Appendf(TEXT("file '%s'\n"), *AbsolutePath);
        }

        const FString ConcatFile = FPaths::Combine(ActiveCaptureDirectory, TEXT("frames.ffconcat"));
        if (!FFileHelper::SaveStringToFile(ConcatBuilder.ToString(), *ConcatFile))
        {
            UE_LOG(LogPanoramaCapture, Error, TEXT("Failed to write ffconcat manifest '%s'."), *ConcatFile);
            return;
        }

        FString CommandInput = FString::Printf(TEXT("-safe 0 -f concat -i \"%s\""), *ConcatFile);

        double AudioOffsetSeconds = 0.0;
        if (!RecordedAudioFile.IsEmpty())
        {
            const double AudioStart = AudioCaptureStartSeconds - CaptureStartSeconds;
            const double FirstFrame = CapturedFrameTimes.Num() > 0 ? CapturedFrameTimes[0] : (FirstVideoTimestamp.IsSet() ? FirstVideoTimestamp.GetValue() : 0.0);
            AudioOffsetSeconds = FMath::Clamp(FirstFrame - AudioStart, -2.0, 2.0);
        }

        FString AudioFile = RecordedAudioFile;
        if (!AssembleWithFFmpeg(CommandInput, AudioFile, OutputVideo, false, AudioOffsetSeconds))
        {
            UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to assemble PNG sequence with FFmpeg."));
        }
        return;
    }

    if (OutputSettings.OutputPath == ECaptureOutputPath::NVENCVideo)
    {
        FinalizeNVENCOutput();
    }
}

void UPanoramaCaptureController::FinalizeNVENCOutput()
{
    if (!OutputSettings.bAutoMuxNVENC)
    {
        if (!ActiveElementaryStream.IsEmpty())
        {
            const FString Extension = OutputSettings.ContainerFormat.IsEmpty() ? TEXT("mp4") : OutputSettings.ContainerFormat;
            const FString TargetPath = BuildVideoFilePath(Extension);
            if (!TargetPath.Equals(ActiveElementaryStream, ESearchCase::CaseSensitive))
            {
                IFileManager::Get().Copy(*TargetPath, *ActiveElementaryStream);
            }
        }
        return;
    }

    if (ActiveElementaryStream.IsEmpty() || !FPaths::FileExists(ActiveElementaryStream))
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("No NVENC elementary stream was produced. Skipping mux."));
        return;
    }

    const FString Extension = OutputSettings.ContainerFormat.IsEmpty() ? TEXT("mp4") : OutputSettings.ContainerFormat;
    const FString OutputVideo = BuildVideoFilePath(Extension);

    FString VideoInput = FString::Printf(TEXT("-framerate %d -i \"%s\""), OutputSettings.FrameRate, *ActiveElementaryStream);

    double AudioOffsetSeconds = 0.0;
    if (!RecordedAudioFile.IsEmpty())
    {
        const double AudioStart = AudioCaptureStartSeconds - CaptureStartSeconds;
        const double FirstFrame = FirstVideoTimestamp.IsSet() ? FirstVideoTimestamp.GetValue() : 0.0;
        AudioOffsetSeconds = FMath::Clamp(FirstFrame - AudioStart, -2.0, 2.0);
    }

    if (!AssembleWithFFmpeg(VideoInput, RecordedAudioFile, OutputVideo, true, AudioOffsetSeconds))
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to mux NVENC stream. Leaving elementary stream at '%s'."), *ActiveElementaryStream);
    }
}

bool UPanoramaCaptureController::AssembleWithFFmpeg(const FString& VideoInputArgs, const FString& AudioFile, const FString& OutputFile, bool bCopyVideoStream, double AudioOffsetSeconds)
{
    const UPanoramaCaptureSettings* Settings = GetDefault<UPanoramaCaptureSettings>();
    if (!Settings || Settings->FFmpegExecutable.IsEmpty())
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("FFmpeg executable not configured. Skipping container assembly."));
        return false;
    }

    if (!FPaths::FileExists(Settings->FFmpegExecutable))
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("FFmpeg executable not found at '%s'."), *Settings->FFmpegExecutable);
        return false;
    }

    FString AudioArgs;
    if (!AudioFile.IsEmpty() && FPaths::FileExists(AudioFile))
    {
        if (!FMath::IsNearlyZero(AudioOffsetSeconds))
        {
            AudioArgs = FString::Printf(TEXT(" -itsoffset %.6f -i \"%s\""), AudioOffsetSeconds, *AudioFile);
        }
        else
        {
            AudioArgs = FString::Printf(TEXT(" -i \"%s\""), *AudioFile);
        }
    }

    FString VideoCodecArgs;
    if (bCopyVideoStream)
    {
        VideoCodecArgs = TEXT(" -c:v copy");
    }
    else
    {
        const bool bUseHEVC = (OutputSettings.NVENC.Codec == ENVENCCodec::HEVC);
        VideoCodecArgs = FString::Printf(TEXT(" -c:v %s"), bUseHEVC ? TEXT("libx265") : TEXT("libx264"));
        if (!bUseHEVC)
        {
            VideoCodecArgs += TEXT(" -pix_fmt yuv420p");
        }
        VideoCodecArgs += TEXT(" -vsync vfr");
    }

    FString ExtraArgs = OutputSettings.FFmpegMuxOverride;
    if (!ExtraArgs.IsEmpty())
    {
        ExtraArgs = TEXT(" ") + ExtraArgs;
    }

    const FString CommandLine = FString::Printf(TEXT("-y %s%s%s%s -shortest \"%s\""),
        *VideoInputArgs,
        *AudioArgs,
        *VideoCodecArgs,
        *ExtraArgs,
        *OutputFile);

    int32 ReturnCode = 0;
    FString StdOut;
    FString StdErr;
    FPlatformProcess::ExecProcess(*Settings->FFmpegExecutable, *CommandLine, &ReturnCode, &StdOut, &StdErr);

    if (ReturnCode != 0)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("FFmpeg failed with code %d. %s"), ReturnCode, *StdErr);
        return false;
    }

    UE_LOG(LogPanoramaCapture, Log, TEXT("FFmpeg assembled output '%s'."), *OutputFile);
    return true;
}

FString UPanoramaCaptureController::BuildFrameFilePath(int32 FrameIndex) const
{
    const FString FileName = FString::Printf(TEXT("%s_%05d.png"), *ActiveBaseFileName, FrameIndex);
    return FPaths::Combine(ActiveCaptureDirectory, FileName);
}

FString UPanoramaCaptureController::BuildVideoFilePath(const FString& Extension) const
{
    FString CleanExtension = Extension;
    if (CleanExtension.StartsWith(TEXT(".")))
    {
        CleanExtension.RightChopInline(1);
    }
    if (CleanExtension.IsEmpty())
    {
        CleanExtension = TEXT("mp4");
    }

    return FPaths::Combine(ActiveCaptureDirectory, FString::Printf(TEXT("%s.%s"), *ActiveBaseFileName, *CleanExtension));
}
