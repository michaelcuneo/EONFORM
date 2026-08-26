using UnrealBuildTool;

public class EonformCore : ModuleRules
{
    public EonformCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // This module has many file-local helper functions. Disable unity so
        // anonymous/file-local helper names remain isolated per translation unit.
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "ImageCore",
            "ImageWrapper"
        });
    }
}
