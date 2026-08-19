using UnrealBuildTool;

public class CodenameGaeaRuntime : ModuleRules
{
    public CodenameGaeaRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "CodenameGaeaCore"
        });
    }
}
