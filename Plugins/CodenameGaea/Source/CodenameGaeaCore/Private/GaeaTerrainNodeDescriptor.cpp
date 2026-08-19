#include "GaeaTerrainNodeDescriptor.h"

#include "GaeaTerrainRecipe.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection DescriptorRegistryMutex;
	TMap<FName, FGaeaTerrainNodeDescriptor> DescriptorRegistry;

	FGaeaTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor NumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		if (Group) Parameter.Group = Group;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor IntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		if (Group) Parameter.Group = Group;
		Parameter.Type = EGaeaTerrainParameterType::Integer;
		Parameter.DefaultInteger = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor BooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		if (Group) Parameter.Group = Group;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor NameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		if (Group) Parameter.Group = Group;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		return Parameter;
	}
}

void FGaeaTerrainNodeDescriptorRegistry::Register(const FGaeaTerrainNodeDescriptor& Descriptor)
{
	if (Descriptor.Type.IsNone()) return;
	FScopeLock Lock(&DescriptorRegistryMutex);
	DescriptorRegistry.Add(Descriptor.Type, Descriptor);
}

void FGaeaTerrainNodeDescriptorRegistry::RegisterBuiltIns()
{
	FScopeLock Lock(&DescriptorRegistryMutex);

	if (!DescriptorRegistry.Contains(GaeaTerrainNodeTypes::SourceDataset))
	{
		FGaeaTerrainNodeDescriptor Source;
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;
		Source.DisplayName = TEXT("Source Dataset");
		Source.Category = TEXT("Input");
		Source.Description = TEXT("Uses a terrain dataset supplied by the evaluation context.");
		Source.Outputs.Add(TerrainPort(TEXT("Terrain")));
		DescriptorRegistry.Add(Source.Type, MoveTemp(Source));
	}

	if (!DescriptorRegistry.Contains(GaeaTerrainNodeTypes::ProceduralTerrain))
	{
		FGaeaTerrainNodeDescriptor Source;
		Source.Type = GaeaTerrainNodeTypes::ProceduralTerrain;
		Source.DisplayName = TEXT("Procedural Terrain");
		Source.Category = TEXT("Generate");
		Source.Description = TEXT("Generates a deterministic normalized fractal heightfield without an external actor or dataset.");
		Source.Outputs.Add(TerrainPort(TEXT("Terrain")));
		Source.Parameters.Add(IntegerParameter(TEXT("Resolution"), TEXT("Resolution"), 257, 2, 1025));
		Source.Parameters.Add(NumberParameter(TEXT("WorldSize"), TEXT("World Size (cm)"), 100000.0, 1.0, 10000000.0));
		Source.Parameters.Add(NumberParameter(TEXT("HeightScale"), TEXT("Height Scale (cm)"), 8000.0, 1.0, 1000000.0));
		Source.Parameters.Add(IntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
		Source.Parameters.Add(NumberParameter(TEXT("Frequency"), TEXT("Frequency"), 0.00055, 0.000001, 1.0));
		Source.Parameters.Add(IntegerParameter(TEXT("Octaves"), TEXT("Octaves"), 6, 1, 16));
		Source.Parameters.Add(NumberParameter(TEXT("Persistence"), TEXT("Persistence"), 0.5, 0.0, 1.0));
		Source.Parameters.Add(NumberParameter(TEXT("Lacunarity"), TEXT("Lacunarity"), 2.0, 1.0, 8.0));
		DescriptorRegistry.Add(Source.Type, MoveTemp(Source));
	}

	if (!DescriptorRegistry.Contains(GaeaTerrainNodeTypes::TerrainShape))
	{
		FGaeaTerrainNodeDescriptor Shape;
		Shape.Type = GaeaTerrainNodeTypes::TerrainShape;
		Shape.DisplayName = TEXT("Terrain Shape");
		Shape.Category = TEXT("Shape");
		Shape.Description = TEXT("Sculpts incoming height using macro form, regional mountains, warp, ridges, foothills, valleys, and plains.");
		Shape.Inputs.Add(TerrainPort(TEXT("Terrain")));
		Shape.Outputs.Add(TerrainPort(TEXT("Terrain")));
		Shape.Parameters.Add(IntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
		Shape.Parameters.Add(NumberParameter(TEXT("MacroStrength"), TEXT("Macro Strength"), 0.75, 0.0, 2.0));
		Shape.Parameters.Add(NumberParameter(TEXT("MountainThreshold"), TEXT("Mountain Threshold"), 0.12, -1.0, 1.0));
		Shape.Parameters.Add(NumberParameter(TEXT("WarpStrength"), TEXT("Warp Strength (cm)"), 4500.0, 0.0, 25000.0));
		Shape.Parameters.Add(NumberParameter(TEXT("RidgeStrength"), TEXT("Ridge Strength"), 0.55, 0.0, 2.0));
		Shape.Parameters.Add(NumberParameter(TEXT("FoothillStrength"), TEXT("Foothill Strength"), 0.28, 0.0, 1.0));
		Shape.Parameters.Add(NumberParameter(TEXT("ValleyDepth"), TEXT("Valley Depth"), 0.16, 0.0, 1.0));
		Shape.Parameters.Add(NumberParameter(TEXT("PlainsStrength"), TEXT("Plains Strength"), 0.55, 0.0, 1.0));
		DescriptorRegistry.Add(Shape.Type, MoveTemp(Shape));
	}

	if (!DescriptorRegistry.Contains(GaeaTerrainNodeTypes::TerrainContext))
	{
		FGaeaTerrainNodeDescriptor Context;
		Context.Type = GaeaTerrainNodeTypes::TerrainContext;
		Context.DisplayName = TEXT("Terrain Context");
		Context.Category = TEXT("Internal");
		Context.Description = TEXT("Internal derived terrain context.");
		Context.bHiddenInGraph = true;
		Context.Inputs.Add(TerrainPort(TEXT("Terrain")));
		Context.Outputs.Add(TerrainPort(TEXT("Terrain")));
		DescriptorRegistry.Add(Context.Type, MoveTemp(Context));
	}

	if (!DescriptorRegistry.Contains(GaeaTerrainNodeTypes::Geology))
	{
		FGaeaTerrainNodeDescriptor Geology;
		Geology.Type = GaeaTerrainNodeTypes::Geology;
		Geology.DisplayName = TEXT("Geology");
		Geology.Category = TEXT("Internal");
		Geology.Description = TEXT("Internal geology data used by terrain processes.");
		Geology.bHiddenInGraph = true;
		Geology.Inputs.Add(TerrainPort(TEXT("Terrain")));
		Geology.Outputs.Add(TerrainPort(TEXT("Terrain")));
		Geology.Parameters.Add(IntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
		Geology.Parameters.Add(NumberParameter(TEXT("Frequency"), TEXT("Lithology Frequency"), 0.000045, 0.000001, 1.0));
		Geology.Parameters.Add(IntegerParameter(TEXT("Octaves"), TEXT("Lithology Octaves"), 3, 1, 16));
		Geology.Parameters.Add(NumberParameter(TEXT("Contrast"), TEXT("Lithology Contrast"), 1.25, 0.1, 4.0));
		Geology.Parameters.Add(NumberParameter(TEXT("MountainHardnessBias"), TEXT("Mountain Hardness Bias"), 0.18, 0.0, 1.0));
		Geology.Parameters.Add(NumberParameter(TEXT("PlainsSoftnessBias"), TEXT("Plains Softness Bias"), 0.15, 0.0, 1.0));
		Geology.Parameters.Add(NumberParameter(TEXT("SoilFormationStrength"), TEXT("Soil Formation Strength"), 0.65, 0.0, 2.0));
		DescriptorRegistry.Add(Geology.Type, MoveTemp(Geology));
	}

	if (!DescriptorRegistry.Contains(GaeaTerrainNodeTypes::ProcessMasks))
	{
		FGaeaTerrainNodeDescriptor Masks;
		Masks.Type = GaeaTerrainNodeTypes::ProcessMasks;
		Masks.DisplayName = TEXT("Process Masks");
		Masks.Category = TEXT("Internal");
		Masks.Description = TEXT("Internal process masks used by terrain simulations.");
		Masks.bHiddenInGraph = true;
		Masks.Inputs.Add(TerrainPort(TEXT("Terrain")));
		Masks.Outputs.Add(TerrainPort(TEXT("Terrain")));
		Masks.Parameters.Add(NumberParameter(TEXT("ThermalTalusAngle"), TEXT("Thermal Talus Angle (deg)"), 34.0, 0.0, 90.0));
		Masks.Parameters.Add(NumberParameter(TEXT("ThermalRegionality"), TEXT("Thermal Regionality"), 0.90, 0.0, 1.0));
		Masks.Parameters.Add(NumberParameter(TEXT("HydraulicRegionality"), TEXT("Hydraulic Regionality"), 0.85, 0.0, 1.0));
		Masks.Parameters.Add(NumberParameter(TEXT("RainfallHighlandBias"), TEXT("Rainfall Highland Bias"), 0.65, 0.0, 1.0));
		Masks.Parameters.Add(NumberParameter(TEXT("EvaporationLowlandBias"), TEXT("Evaporation Lowland Bias"), 0.55, 0.0, 1.0));
		DescriptorRegistry.Add(Masks.Type, MoveTemp(Masks));
	}

	if (!DescriptorRegistry.Contains(GaeaTerrainNodeTypes::Slope))
	{
		FGaeaTerrainNodeDescriptor Slope;
		Slope.Type = GaeaTerrainNodeTypes::Slope;
		Slope.DisplayName = TEXT("Slope");
		Slope.Category = TEXT("Data");
		Slope.Description = TEXT("Creates a scalar selection mask for terrain within a slope range.");
		Slope.Inputs.Add(TerrainPort(TEXT("Terrain")));
		Slope.Outputs.Add(ScalarPort(TEXT("Mask")));
		Slope.Parameters.Add(NumberParameter(TEXT("Min"), TEXT("Min"), 0.0, 0.0, 90.0));
		Slope.Parameters.Add(NumberParameter(TEXT("Max"), TEXT("Max"), 45.0, 0.0, 90.0));
		Slope.Parameters.Add(NumberParameter(TEXT("Falloff"), TEXT("Falloff"), 5.0, 0.0, 45.0));
		DescriptorRegistry.Add(Slope.Type, MoveTemp(Slope));
	}

	if (!DescriptorRegistry.Contains(GaeaTerrainNodeTypes::HydraulicErosion))
	{
		FGaeaTerrainNodeDescriptor Erosion;
		Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
		Erosion.DisplayName = TEXT("Erosion");
		Erosion.Category = TEXT("Erosion");
		Erosion.Description = TEXT("Gaea-style hydraulic erosion with routable Wear, Deposits, and Flow outputs.");
		Erosion.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		Erosion.Inputs.Add(ScalarPort(TEXT("Mask"), TEXT("Area Mask")));
		Erosion.Inputs.Add(ScalarPort(TEXT("Sediment"), TEXT("Sediment")));
		Erosion.Outputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Out")));
		Erosion.Outputs.Add(ScalarPort(TEXT("Wear"), TEXT("Wear")));
		Erosion.Outputs.Add(ScalarPort(TEXT("Deposits"), TEXT("Deposits")));
		Erosion.Outputs.Add(ScalarPort(TEXT("Flow"), TEXT("Flow")));

		Erosion.Parameters.Add(IntegerParameter(TEXT("Iterations"), TEXT("Duration"), 24, 1, 4096, TEXT("Erosion")));
		Erosion.Parameters.Add(NumberParameter(TEXT("RockSoftness"), TEXT("Rock Softness"), 0.0, 0.0, 1.0, TEXT("Erosion")));
		Erosion.Parameters.Add(NumberParameter(TEXT("Strength"), TEXT("Strength"), 1.0, 0.0, 4.0, TEXT("Erosion")));
		Erosion.Parameters.Add(NumberParameter(TEXT("Downcutting"), TEXT("Downcutting"), 0.5, 0.0, 2.0, TEXT("Downcutting")));
		Erosion.Parameters.Add(NumberParameter(TEXT("Inhibition"), TEXT("Inhibition"), 0.0, 0.0, 1.0, TEXT("Downcutting")));
		Erosion.Parameters.Add(NumberParameter(TEXT("BaseLevel"), TEXT("Base Level"), -1.0, -1.0, 1.0, TEXT("Downcutting")));
		Erosion.Parameters.Add(NumberParameter(TEXT("FeatureScale"), TEXT("Feature Scale"), 1.0, 0.25, 8.0, TEXT("Scale")));
		Erosion.Parameters.Add(NumberParameter(TEXT("Debris"), TEXT("Debris"), 0.5, 0.0, 1.0, TEXT("Flow")));
		Erosion.Parameters.Add(NumberParameter(TEXT("Volume"), TEXT("Volume"), 1.0, 0.0, 4.0, TEXT("Flow")));
		Erosion.Parameters.Add(NumberParameter(TEXT("SedimentRemoval"), TEXT("Sediment Removal"), 0.0, 0.0, 1.0, TEXT("Flow")));
		Erosion.Parameters.Add(NameParameter(
			TEXT("SelectiveProcessing"), TEXT("Selective Processing"), TEXT("None"),
			{ TEXT("None"), TEXT("ErosionStrength"), TEXT("RockSoftness"), TEXT("Precipitation") },
			TEXT("Selective Processing")));
		Erosion.Parameters.Add(IntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
		Erosion.Parameters.Add(BooleanParameter(TEXT("AggressiveMode"), TEXT("Aggressive Mode"), false));
		Erosion.Parameters.Add(BooleanParameter(TEXT("Deterministic"), TEXT("Deterministic"), true));
		DescriptorRegistry.Add(Erosion.Type, MoveTemp(Erosion));
	}
}

bool FGaeaTerrainNodeDescriptorRegistry::Get(FName NodeType, FGaeaTerrainNodeDescriptor& OutDescriptor)
{
	RegisterBuiltIns();
	FScopeLock Lock(&DescriptorRegistryMutex);
	const FGaeaTerrainNodeDescriptor* Descriptor = DescriptorRegistry.Find(NodeType);
	if (!Descriptor) return false;
	OutDescriptor = *Descriptor;
	return true;
}

void FGaeaTerrainNodeDescriptorRegistry::GetAll(TArray<FGaeaTerrainNodeDescriptor>& OutDescriptors)
{
	RegisterBuiltIns();
	FScopeLock Lock(&DescriptorRegistryMutex);
	OutDescriptors.Reset();
	DescriptorRegistry.GenerateValueArray(OutDescriptors);
	OutDescriptors.Sort([](const FGaeaTerrainNodeDescriptor& A, const FGaeaTerrainNodeDescriptor& B)
	{
		if (A.Category != B.Category) return A.Category < B.Category;
		return A.DisplayName < B.DisplayName;
	});
}

void FGaeaTerrainNodeDescriptorRegistry::Reset()
{
	FScopeLock Lock(&DescriptorRegistryMutex);
	DescriptorRegistry.Reset();
}
