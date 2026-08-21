using System.IO;
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

        // UE 5.8's experimental MeshPartitionEditor public headers currently include
        // MaterialCache/MaterialCacheVirtualTexture.h from Engine's private headers.
        // MaterialCache is not a standalone UBT module, so expose the Engine private
        // include root explicitly while this experimental API requires it.
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/Engine/Private"));
    }
}
