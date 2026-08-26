using UnrealBuildTool;

public class EonformMeshTerrainEditor : ModuleRules
{
    public EonformMeshTerrainEditor(ReadOnlyTargetRules Target) : base(Target)
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
            "DynamicMesh",
            "GeometryCore",
            "GeometryFramework",
            "MeshPartition",
            "MeshPartitionEditor",
            "UnrealEd"
        });
    }
}
