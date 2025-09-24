#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPanoramaNVENC, Log, All);

class FPanoramaCaptureNVENCModule : public IModuleInterface
{
public:
    static inline FPanoramaCaptureNVENCModule& Get()
    {
        return FModuleManager::LoadModuleChecked<FPanoramaCaptureNVENCModule>(TEXT("PanoramaCaptureNVENC"));
    }

    static inline bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded(TEXT("PanoramaCaptureNVENC"));
    }

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
