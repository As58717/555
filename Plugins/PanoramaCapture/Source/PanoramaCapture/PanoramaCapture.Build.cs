using UnrealBuildTool;

public class PanoramaCapture : ModuleRules
{
    public PanoramaCapture(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "RenderCore",
            "RHI",
            "Projects",
            "Slate",
            "SlateCore"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "InputCore",
            "CinematicCamera",
            "DeveloperSettings",
            "AudioMixer",
            "ImageWrapper",
            "SignalProcessing",
            "MovieScene",
            "MovieSceneTracks",
            "TimeManagement"
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDependencyModuleNames.Add("D3D12RHI");
            PrivateDependencyModuleNames.Add("D3D11RHI");
            PublicDefinitions.Add("WITH_PANORAMA_NVENC=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_PANORAMA_NVENC=0");
        }
    }
}
