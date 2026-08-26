using UnrealBuildTool;

public class EonformEditor : ModuleRules
{
    public EonformEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "EonformCore",
            "EonformRuntime"
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
            "EonformMeshTerrainEditor",
            "Projects",
            "PropertyEditor",
            "UnrealEd",
            "Slate",
            "SlateCore",
            "ToolMenus"
        });
    }
}
