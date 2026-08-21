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
            "CodenameGaeaRuntime"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "DynamicMesh",
            "GeometryCore",
            "GeometryFramework",
            "MeshPartition",
            "MeshPartitionEditor",
            "UnrealEd"
        });
    }
}
