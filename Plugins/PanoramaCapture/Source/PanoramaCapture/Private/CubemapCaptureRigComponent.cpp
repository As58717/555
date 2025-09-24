#include "CubemapCaptureRigComponent.h"

#include "Camera/CameraTypes.h"
#include "Engine/TextureRenderTarget2D.h"

namespace
{
    constexpr int32 FacesPerEye = 6;

    const FRotator FaceRotations[FacesPerEye] = {
        FRotator(0.f, 90.f, 0.f),   // +X
        FRotator(0.f, -90.f, 0.f),  // -X
        FRotator(-90.f, 0.f, 0.f),  // +Y
        FRotator(90.f, 0.f, 0.f),   // -Y
        FRotator(0.f, 0.f, 0.f),    // +Z (forward)
        FRotator(0.f, 180.f, 0.f)   // -Z (back)
    };

    const TCHAR* FaceNames[FacesPerEye] = {
        TEXT("+X"),
        TEXT("-X"),
        TEXT("+Y"),
        TEXT("-Y"),
        TEXT("+Z"),
        TEXT("-Z")
    };
}

UCubemapCaptureRigComponent::UCubemapCaptureRigComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;

    bStereo = false;
    NearClipPlane = 10.f;
    FarClipPlane = 500000.f;

    if (Faces.Num() == 0)
    {
        for (int32 FaceIndex = 0; FaceIndex < FacesPerEye; ++FaceIndex)
        {
            FPanoramaCaptureFace Face;
            Face.Name = FName(FaceNames[FaceIndex]);
            Face.Rotation = FaceRotations[FaceIndex];
            Face.DebugColor = FColor::MakeRandomColor();
            Faces.Add(Face);
        }
    }
}

void UCubemapCaptureRigComponent::OnRegister()
{
    Super::OnRegister();
    InitializeRig();
}

void UCubemapCaptureRigComponent::OnUnregister()
{
    ReleaseRig();
    Super::OnUnregister();
}

void UCubemapCaptureRigComponent::InitializeRig()
{
    const int32 EyeCount = bStereo ? 2 : 1;
    const int32 RequiredFaces = EyeCount * FacesPerEye;
    FaceCaptures.Reserve(RequiredFaces);
    EyeRenderTargets.SetNum(RequiredFaces);

    for (int32 EyeIndex = 0; EyeIndex < EyeCount; ++EyeIndex)
    {
        EnsureFaceCaptures(EyeIndex);
    }

    UpdateCaptureTransforms();
}

void UCubemapCaptureRigComponent::ReleaseRig()
{
    for (TObjectPtr<USceneCaptureComponent2D>& Capture : FaceCaptures)
    {
        if (Capture)
        {
            Capture->DestroyComponent();
        }
    }
    FaceCaptures.Empty();
    EyeRenderTargets.Empty();
}

void UCubemapCaptureRigComponent::TickRig(float DeltaTime)
{
    if (!IsRegistered())
    {
        return;
    }

    UpdateCaptureTransforms();

    for (TObjectPtr<USceneCaptureComponent2D>& Capture : FaceCaptures)
    {
        if (Capture)
        {
            Capture->CaptureScene();
        }
    }
}

UTextureRenderTarget2D* UCubemapCaptureRigComponent::GetFaceRenderTarget(int32 FaceIndex, bool bLeftEye) const
{
    const int32 EyeIndex = bLeftEye ? 0 : (bStereo ? 1 : 0);
    const int32 Index = EyeIndex * FacesPerEye + FaceIndex;
    if (EyeRenderTargets.IsValidIndex(Index))
    {
        return EyeRenderTargets[Index].Get();
    }
    return nullptr;
}

void UCubemapCaptureRigComponent::SetCaptureMaterial(UMaterialInterface* OverrideMaterial)
{
    CaptureMaterial = OverrideMaterial;
    for (TObjectPtr<USceneCaptureComponent2D>& Capture : FaceCaptures)
    {
        if (Capture)
        {
            Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
            Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
            Capture->PostProcessSettings.bOverride_ColorGradingLUT = (CaptureMaterial != nullptr);
            Capture->PostProcessSettings.AddBlendable(CaptureMaterial, 1.0f);
        }
    }
}

void UCubemapCaptureRigComponent::EnsureFaceCaptures(int32 EyeIndex)
{
    const int32 StartIndex = EyeIndex * FacesPerEye;
    const int32 RequiredFaces = StartIndex + FacesPerEye;

    if (FaceCaptures.Num() < RequiredFaces)
    {
        FaceCaptures.SetNum(RequiredFaces);
    }

    if (EyeRenderTargets.Num() < RequiredFaces)
    {
        EyeRenderTargets.SetNum(RequiredFaces);
    }

    for (int32 FaceIndex = 0; FaceIndex < FacesPerEye; ++FaceIndex)
    {
        const int32 CaptureIndex = StartIndex + FaceIndex;
        TObjectPtr<USceneCaptureComponent2D>& Capture = FaceCaptures[CaptureIndex];

        if (!Capture)
        {
            Capture = NewObject<USceneCaptureComponent2D>(GetOwner());
            Capture->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetIncludingScale);
            Capture->SetRelativeLocation(FVector::ZeroVector);
            Capture->SetRelativeRotation(FRotator::ZeroRotator);
            Capture->RegisterComponent();
            ConfigureCaptureComponent(Capture);
        }

        const EPixelFormat PixelFormat = OutputSettings.bUse16BitPNG ? PF_FloatRGBA : PF_B8G8R8A8;
        const int32 Width = OutputSettings.Resolution.Width;
        const int32 Height = OutputSettings.Resolution.Height;
        UTextureRenderTarget2D* RenderTarget = EyeRenderTargets[CaptureIndex].Get();
        if (!RenderTarget)
        {
            RenderTarget = NewObject<UTextureRenderTarget2D>(this);
            RenderTarget->ClearColor = FLinearColor::Black;
            EyeRenderTargets[CaptureIndex] = RenderTarget;
        }

        if (RenderTarget->SizeX != Width || RenderTarget->SizeY != Height || RenderTarget->OverrideFormat != PixelFormat)
        {
            RenderTarget->InitCustomFormat(Width, Height, PixelFormat, false);
            RenderTarget->TargetGamma = (OutputSettings.GammaSpace == EPanoramaGammaSpace::Linear) ? 1.0f : 2.2f;
            RenderTarget->UpdateResourceImmediate(true);
        }

        if (Capture)
        {
            Capture->TextureTarget = RenderTarget;
        }
    }
}

void UCubemapCaptureRigComponent::UpdateCaptureTransforms()
{
    const int32 EyeCount = bStereo ? 2 : 1;
    for (int32 EyeIndex = 0; EyeIndex < EyeCount; ++EyeIndex)
    {
        const bool bLeftEye = (EyeIndex == 0);
        float EyeOffset = 0.0f;
        if (EyeCount > 1)
        {
            const float HalfIPDMeters = (OutputSettings.InterpupillaryDistanceCm * 0.5f) / 100.0f;
            EyeOffset = bLeftEye ? -HalfIPDMeters : HalfIPDMeters;
        }
        const FVector EyeTranslation = GetRightVector() * EyeOffset;
        const FRotator ToeInAdjustment = (EyeCount > 1 && OutputSettings.bUseStereoToeIn)
            ? FRotator(0.0f, bLeftEye ? OutputSettings.ToeInAngleDegrees : -OutputSettings.ToeInAngleDegrees, 0.0f)
            : FRotator::ZeroRotator;

        for (int32 FaceIndex = 0; FaceIndex < FacesPerEye; ++FaceIndex)
        {
            const int32 CaptureIndex = EyeIndex * FacesPerEye + FaceIndex;
            if (FaceCaptures.IsValidIndex(CaptureIndex))
            {
                if (USceneCaptureComponent2D* Capture = FaceCaptures[CaptureIndex])
                {
                    const FRotator DesiredRotation = Faces.IsValidIndex(FaceIndex) ? Faces[FaceIndex].Rotation : FaceRotations[FaceIndex];
                    Capture->SetWorldLocation(GetComponentLocation() + EyeTranslation);
                    Capture->SetWorldRotation(DesiredRotation + ToeInAdjustment + GetComponentRotation());
                }
            }
        }
    }
}

void UCubemapCaptureRigComponent::ConfigureCaptureComponent(USceneCaptureComponent2D* Capture) const
{
    if (!Capture)
    {
        return;
    }

    Capture->bCaptureEveryFrame = false;
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
    Capture->FOVAngle = 90.f;
    Capture->ClipPlaneNear = NearClipPlane;
    Capture->ClipPlaneFar = FarClipPlane;
    Capture->bEnableClipPlane = false;
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
    Capture->CompositeMode = ESceneCaptureCompositeMode::SCCM_Overwrite;
    Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
}
