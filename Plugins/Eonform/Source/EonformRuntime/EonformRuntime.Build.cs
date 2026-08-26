using UnrealBuildTool;

public class EonformRuntime : ModuleRules
{
    public EonformRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "GeometryCore",
            "GeometryFramework",
            "EonformCore"
        });
    }
}
