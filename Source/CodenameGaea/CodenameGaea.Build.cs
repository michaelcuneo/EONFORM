// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class CodenameGaea : ModuleRules
{
	public CodenameGaea(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"GeometryCore",
			"GeometryFramework"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
