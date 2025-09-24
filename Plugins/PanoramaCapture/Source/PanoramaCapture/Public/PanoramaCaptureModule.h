#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "Templates/Function.h"
#include "VideoEncoder.h"

class FPanoramaCaptureModule : public IModuleInterface
{
public:
    static inline FPanoramaCaptureModule& Get()
    {
        return FModuleManager::LoadModuleChecked<FPanoramaCaptureModule>(TEXT("PanoramaCapture"));
    }

    static inline bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded(TEXT("PanoramaCapture"));
    }

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    void RegisterVideoEncoderFactory(TFunction<TSharedPtr<IPanoramaVideoEncoder>()> InFactory);
    void UnregisterVideoEncoderFactory();
    TSharedPtr<IPanoramaVideoEncoder> CreateVideoEncoder() const;

private:
    void RegisterSettings();
    void UnregisterSettings();

    bool bShaderDirectoryRegistered = false;
    TFunction<TSharedPtr<IPanoramaVideoEncoder>()> EncoderFactory;
};

DECLARE_LOG_CATEGORY_EXTERN(LogPanoramaCapture, Log, All);
