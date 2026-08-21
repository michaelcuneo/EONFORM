using UnrealBuildTool;

public class CodenameGaeaCore : ModuleRules
{
    public CodenameGaeaCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

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
