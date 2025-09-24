using UnrealBuildTool;
using System;
using System.IO;

public class PanoramaCaptureNVENC : ModuleRules
{
    public PanoramaCaptureNVENC(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "RHI",
            "RenderCore",
            "PanoramaCapture"
        });

        PrivateDependencyModuleNames.Add("Projects");

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("WITH_PANORAMA_NVENC=1");
            PrivateDependencyModuleNames.Add("D3D11RHI");
            PrivateDependencyModuleNames.Add("D3D12RHI");
            PublicDelayLoadDLLs.Add("nvEncodeAPI64.dll");

            string NvencSdk = Environment.GetEnvironmentVariable("NVENC_SDK_DIR");
            if (!string.IsNullOrEmpty(NvencSdk))
            {
                string IncludePath = Path.Combine(NvencSdk, "Include");
                if (Directory.Exists(IncludePath))
                {
                    PrivateIncludePaths.Add(IncludePath);
                }
            }
        }
        else
        {
            PublicDefinitions.Add("WITH_PANORAMA_NVENC=0");
        }
    }
}
