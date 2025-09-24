#include "PanoramaCaptureNVENCModule.h"

#include "PanoramaCaptureModule.h"
#include "NVENCEncoder.h"

DEFINE_LOG_CATEGORY(LogPanoramaNVENC);

IMPLEMENT_MODULE(FPanoramaCaptureNVENCModule, PanoramaCaptureNVENC)

void FPanoramaCaptureNVENCModule::StartupModule()
{
#if !WITH_PANORAMA_NVENC
    UE_LOG(LogPanoramaNVENC, Warning, TEXT("Panorama NVENC module initialized without platform support."));
#else
    FPanoramaCaptureModule::Get().RegisterVideoEncoderFactory([]()
    {
        return MakeShared<FPanoramaNVENCEncoder, ESPMode::ThreadSafe>();
    });
    UE_LOG(LogPanoramaNVENC, Log, TEXT("Panorama NVENC module ready."));
#endif
}

void FPanoramaCaptureNVENCModule::ShutdownModule()
{
#if WITH_PANORAMA_NVENC
    if (FPanoramaCaptureModule::IsAvailable())
    {
        FPanoramaCaptureModule::Get().UnregisterVideoEncoderFactory();
    }
#endif
}
