using UnrealBuildTool;

public class CodenameGaeaEditor : ModuleRules
{
    public CodenameGaeaEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "CodenameGaeaCore",
            "CodenameGaeaRuntime"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AssetRegistry",
            "AssetTools",
            "ContentBrowser",
            "GraphEditor",
            "InputCore",
            "UnrealEd",
            "Slate",
            "SlateCore",
            "ToolMenus"
        });
    }
}
