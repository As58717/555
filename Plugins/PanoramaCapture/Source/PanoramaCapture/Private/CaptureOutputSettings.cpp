#include "CaptureOutputSettings.h"

UPanoramaCaptureSettings::UPanoramaCaptureSettings()
{
    CategoryName = TEXT("Plugins");
    SectionName = TEXT("PanoramaCapture");
    NVENCProfile = TEXT("main10");
    AudioSubmix = NAME_None;
    DefaultOutputDirectory = TEXT("PanoramaCaptures");
    bDefaultAutoAssemble = true;
    FFmpegExecutable = TEXT("");
}
