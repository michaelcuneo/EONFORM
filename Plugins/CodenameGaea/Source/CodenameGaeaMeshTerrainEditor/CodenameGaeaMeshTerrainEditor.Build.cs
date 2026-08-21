using UnrealBuildTool;

public class CodenameGaeaMeshTerrainEditor : ModuleRules
{
    public CodenameGaeaMeshTerrainEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "CodenameGaeaCore",
            "CodenameGaeaRuntime",
            "MeshPartition",
            "MeshPartitionEditor"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "DynamicMesh",
            "GeometryCore",
            "GeometryFramework",
            "UnrealEd"
        });
    }
}
