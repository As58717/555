using UnrealBuildTool;

public class PanoramaCaptureEditor : ModuleRules
{
    public PanoramaCaptureEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "EditorSubsystem",
            "UnrealEd",
            "LevelEditor",
            "Projects",
            "PanoramaCapture"
        });
    }
}
