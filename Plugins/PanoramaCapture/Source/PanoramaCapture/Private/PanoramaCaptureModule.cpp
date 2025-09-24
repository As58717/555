#include "PanoramaCaptureModule.h"
#include "CaptureOutputSettings.h"
#include "CubemapCaptureRigComponent.h"
#include "DeveloperSettingsModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogPanoramaCapture);

IMPLEMENT_MODULE(FPanoramaCaptureModule, PanoramaCapture)

void FPanoramaCaptureModule::StartupModule()
{
    if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PanoramaCapture")))
    {
        const FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
        AddShaderSourceDirectoryMapping(TEXT("/PanoramaCapture"), PluginShaderDir);
        bShaderDirectoryRegistered = true;
    }

    RegisterSettings();
}

void FPanoramaCaptureModule::ShutdownModule()
{
    if (bShaderDirectoryRegistered)
    {
        RemoveShaderSourceDirectoryMapping(TEXT("/PanoramaCapture"));
        bShaderDirectoryRegistered = false;
    }
    UnregisterSettings();
}

void FPanoramaCaptureModule::RegisterSettings()
{
#if WITH_EDITOR
    if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        SettingsModule->RegisterSettings("Project", "Plugins", "PanoramaCapture",
            NSLOCTEXT("PanoramaCapture", "SettingsName", "Panorama Capture"),
            NSLOCTEXT("PanoramaCapture", "SettingsDescription", "Configure cubemap and equirectangular capture defaults."),
            GetMutableDefault<UPanoramaCaptureSettings>());
    }
#endif
}

void FPanoramaCaptureModule::UnregisterSettings()
{
#if WITH_EDITOR
    if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        SettingsModule->UnregisterSettings("Project", "Plugins", "PanoramaCapture");
    }
#endif
}

void FPanoramaCaptureModule::RegisterVideoEncoderFactory(TFunction<TSharedPtr<IPanoramaVideoEncoder>()> InFactory)
{
    EncoderFactory = MoveTemp(InFactory);
}

void FPanoramaCaptureModule::UnregisterVideoEncoderFactory()
{
    EncoderFactory = nullptr;
}

TSharedPtr<IPanoramaVideoEncoder> FPanoramaCaptureModule::CreateVideoEncoder() const
{
    return EncoderFactory ? EncoderFactory() : nullptr;
}
