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
            "DesktopPlatform",
            "DynamicMesh",
            "GeometryCore",
            "GeometryFramework",
            "GraphEditor",
            "InputCore",
            "CodenameGaeaMeshTerrainEditor",
            "Projects",
            "PropertyEditor",
            "UnrealEd",
            "Slate",
            "SlateCore",
            "ToolMenus"
        });
    }
}
