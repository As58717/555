#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CaptureFrameQueue.h"
#include "CaptureOutputSettings.h"

#include "Async/Future.h"
#include "VideoEncoder.h"
#include "Templates/Optional.h"

#include "Templates/SharedPointer.h"

#include "PanoramaCaptureController.generated.h"

class UCubemapCaptureRigComponent;
class UAudioComponent;
class USoundSubmix;
class USoundSubmixBase;
class UTexture2D;
class UTextRenderComponent;
class FRHIGPUTextureReadback;
class IPanoramaVideoEncoder;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPanoramaCaptureStatusChanged, FName, NewStatus);

UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent))
class PANORAMACAPTURE_API UPanoramaCaptureController : public UActorComponent
{
    GENERATED_BODY()

public:
    UPanoramaCaptureController();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FCaptureOutputSettings OutputSettings;

    UPROPERTY(BlueprintAssignable, Category = "Capture")
    FPanoramaCaptureStatusChanged OnStatusChanged;

    UFUNCTION(BlueprintCallable, Category = "Capture")
    void StartCapture();

    UFUNCTION(BlueprintCallable, Category = "Capture")
    void StopCapture();

    UFUNCTION(BlueprintCallable, Category = "Capture")
    bool IsCapturing() const { return bIsCapturing; }

    UFUNCTION(BlueprintCallable, Category = "Capture")
    int32 GetDroppedFrameCount() const { return FrameBuffer.GetDroppedFrames(); }

    UFUNCTION(BlueprintCallable, Category = "Capture")
    int32 GetBufferedFrameCount() const { return FrameBuffer.Num(); }

    UFUNCTION(BlueprintCallable, Category = "Capture")
    int32 GetBlockedFrameCount() const { return FrameBuffer.GetBlockedFrames(); }

    UFUNCTION(BlueprintCallable, Category = "Capture")
    UTexture2D* GetPreviewTexture() const { return PreviewTexture; }

    UFUNCTION(BlueprintCallable, Category = "Capture")
    FString GetActiveCaptureDirectory() const { return ActiveCaptureDirectory; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    void EnsureRig();
    void CaptureFrame();
    void ConsumeFrameQueue();
    void UpdateStatus(FName NewStatus);
    void InitializeRingBuffer();
    void InitializeOutputDirectory();
    void EnsureStatusDisplay();

    void InitializeAudioCapture();
    void ShutdownAudioCapture();
    void ProcessPendingReadbacks();
    void WritePNGFrame(FPanoramaCaptureFrame&& Frame);
    void UpdatePreviewFromFrame(const FPanoramaCaptureFrame& Frame);
    void FinalizeCaptureOutputs();
    void FinalizeNVENCOutput();
    bool AssembleWithFFmpeg(const FString& InputVideo, const FString& AudioFile, const FString& Container, bool bCopyVideoStream, double AudioOffsetSeconds);
    FString BuildFrameFilePath(int32 FrameIndex) const;
    FString BuildVideoFilePath(const FString& Extension) const;

    UPROPERTY()
    TObjectPtr<UCubemapCaptureRigComponent> ManagedRig;

    FCaptureFrameRingBuffer FrameBuffer;
    FTimerHandle CaptureTimerHandle;
    bool bIsCapturing;
    double CaptureStartSeconds;
    int32 CaptureFrameCounter;

    TArray<TSharedPtr<class FPendingCapturePayload, ESPMode::ThreadSafe>> PendingReadbacks;
    TSharedPtr<IPanoramaVideoEncoder> ActiveEncoder;
    FString ActiveCaptureDirectory;
    FString ActiveBaseFileName;
    FString ActiveElementaryStream;
    FString RecordedAudioFile;
    TArray<FString> CapturedFrameFiles;
    TArray<TFuture<void>> PendingWriteTasks;
    TArray<double> CapturedFrameTimes;
    TOptional<double> FirstVideoTimestamp;
    TOptional<double> LastVideoTimestamp;
    double AudioCaptureStartSeconds;

    FName CurrentStatus;
    double LastStatusUpdateSeconds;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> PreviewTexture;

    TWeakObjectPtr<USoundSubmixBase> RecordedSubmix;
    UPROPERTY()
    TObjectPtr<UTextRenderComponent> StatusBillboard;
};
