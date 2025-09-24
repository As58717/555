#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/SceneCapture2D.h"
#include "CaptureOutputSettings.h"
#include "CubemapCaptureRigComponent.generated.h"

class UTextureRenderTarget2D;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FPanoramaCaptureFace
{
    GENERATED_BODY()

    FPanoramaCaptureFace()
        : Rotation(FRotator::ZeroRotator)
        , DebugColor(FColor::White)
    {
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FRotator Rotation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FColor DebugColor;
};

UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent))
class PANORAMACAPTURE_API UCubemapCaptureRigComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UCubemapCaptureRigComponent();

    UPROPERTY(Transient)
    TArray<TObjectPtr<USceneCaptureComponent2D>> FaceCaptures;

    UPROPERTY(EditAnywhere, Category = "Capture")
    FCaptureOutputSettings OutputSettings;

    UPROPERTY(EditAnywhere, Category = "Capture")
    bool bStereo;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float NearClipPlane;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float FarClipPlane;

    UPROPERTY(EditAnywhere, Category = "Capture")
    TArray<FPanoramaCaptureFace> Faces;

    virtual void OnRegister() override;
    virtual void OnUnregister() override;

    void InitializeRig();
    void ReleaseRig();

    void TickRig(float DeltaTime);

    UTextureRenderTarget2D* GetFaceRenderTarget(int32 FaceIndex, bool bLeftEye) const;

    void SetCaptureMaterial(UMaterialInterface* OverrideMaterial);

protected:
    void EnsureFaceCaptures(int32 EyeIndex);
    void UpdateCaptureTransforms();

private:
    void ConfigureCaptureComponent(USceneCaptureComponent2D* Capture) const;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> CaptureMaterial;

    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<UTextureRenderTarget2D>> EyeRenderTargets;
};
