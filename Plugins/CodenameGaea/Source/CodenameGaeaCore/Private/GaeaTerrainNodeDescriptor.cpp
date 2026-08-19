#include "GaeaTerrainNodeDescriptor.h"

#include "GaeaTerrainRecipe.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection DescriptorRegistryMutex;
	TMap<FName, FGaeaTerrainNodeDescriptor> DescriptorRegistry;

	FGaeaTerrainPortDescriptor TerrainPort(FName Name)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("TerrainDataset");
		return Port;
	}

	FGaeaTerrainParameterDescriptor NumberParameter(
		FName Name,
		const TCHAR* DisplayName,
		double DefaultValue,
		double Minimum,
		double Maximum)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor IntegerParameter(
		FName Name,
		const TCHAR* DisplayName,
		int64 DefaultValue,
		int64 Minimum,
		int64 Maximum)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Integer;
		Parameter.DefaultInteger = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}
}

void FGaeaTerrainNodeDescriptorRegistry::Register(const FGaeaTerrainNodeDescriptor& Descriptor)
{
	if (Descriptor.Type.IsNone())
	{
		return;
	}

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

	if (!DescriptorRegistry.Contains(GaeaTerrainNodeTypes::TerrainContext))
	{
		FGaeaTerrainNodeDescriptor Context;
		Context.Type = GaeaTerrainNodeTypes::TerrainContext;
		Context.DisplayName = TEXT("Terrain Context");
		Context.Category = TEXT("Analyze");
		Context.Description = TEXT("Derives elevation, slope, curvature, mountain, foothill, and plains fields from terrain height.");
		Context.Inputs.Add(TerrainPort(TEXT("Terrain")));
		Context.Outputs.Add(TerrainPort(TEXT("Terrain")));
		DescriptorRegistry.Add(Context.Type, MoveTemp(Context));
	}

	if (!DescriptorRegistry.Contains(GaeaTerrainNodeTypes::ProcessMasks))
	{
		FGaeaTerrainNodeDescriptor Masks;
		Masks.Type = GaeaTerrainNodeTypes::ProcessMasks;
		Masks.DisplayName = TEXT("Process Masks");
		Masks.Category = TEXT("Analyze");
		Masks.Description = TEXT("Derives thermal, rainfall, hydraulic erosion, deposition, and evaporation masks from Terrain Context fields.");
		Masks.Inputs.Add(TerrainPort(TEXT("Terrain")));
		Masks.Outputs.Add(TerrainPort(TEXT("Terrain")));
		Masks.Parameters.Add(NumberParameter(TEXT("ThermalTalusAngle"), TEXT("Thermal Talus Angle (deg)"), 34.0, 0.0, 90.0));
		Masks.Parameters.Add(NumberParameter(TEXT("ThermalRegionality"), TEXT("Thermal Regionality"), 0.90, 0.0, 1.0));
		Masks.Parameters.Add(NumberParameter(TEXT("HydraulicRegionality"), TEXT("Hydraulic Regionality"), 0.85, 0.0, 1.0));
		Masks.Parameters.Add(NumberParameter(TEXT("RainfallHighlandBias"), TEXT("Rainfall Highland Bias"), 0.65, 0.0, 1.0));
		Masks.Parameters.Add(NumberParameter(TEXT("EvaporationLowlandBias"), TEXT("Evaporation Lowland Bias"), 0.55, 0.0, 1.0));
		DescriptorRegistry.Add(Masks.Type, MoveTemp(Masks));
	}

	if (!DescriptorRegistry.Contains(GaeaTerrainNodeTypes::HydraulicErosion))
	{
		FGaeaTerrainNodeDescriptor Erosion;
		Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
		Erosion.DisplayName = TEXT("Hydraulic Erosion");
		Erosion.Category = TEXT("Simulate");
		Erosion.Description = TEXT("Simulates rainfall, flow, erosion, sediment transport, deposition, and evaporation.");
		Erosion.Inputs.Add(TerrainPort(TEXT("Terrain")));
		Erosion.Outputs.Add(TerrainPort(TEXT("Terrain")));
		Erosion.Parameters.Add(IntegerParameter(TEXT("Iterations"), TEXT("Iterations"), 24, 1, 4096));
		Erosion.Parameters.Add(NumberParameter(TEXT("Rainfall"), TEXT("Rainfall"), 0.01, 0.0, 1.0));
		Erosion.Parameters.Add(NumberParameter(TEXT("FlowRate"), TEXT("Flow Rate"), 0.55, 0.0, 1.0));
		Erosion.Parameters.Add(NumberParameter(TEXT("SedimentCapacity"), TEXT("Sediment Capacity"), 0.7, 0.0, 8.0));
		Erosion.Parameters.Add(NumberParameter(TEXT("ErosionRate"), TEXT("Erosion Rate"), 0.18, 0.0, 1.0));
		Erosion.Parameters.Add(NumberParameter(TEXT("DepositionRate"), TEXT("Deposition Rate"), 0.12, 0.0, 1.0));
		Erosion.Parameters.Add(NumberParameter(TEXT("Evaporation"), TEXT("Evaporation"), 0.08, 0.0, 1.0));
		Erosion.Parameters.Add(NumberParameter(TEXT("MinimumSlope"), TEXT("Minimum Slope"), 0.01, 0.0, 1.0));
		DescriptorRegistry.Add(Erosion.Type, MoveTemp(Erosion));
	}
}

bool FGaeaTerrainNodeDescriptorRegistry::Get(FName NodeType, FGaeaTerrainNodeDescriptor& OutDescriptor)
{
	RegisterBuiltIns();
	FScopeLock Lock(&DescriptorRegistryMutex);
	const FGaeaTerrainNodeDescriptor* Descriptor = DescriptorRegistry.Find(NodeType);
	if (!Descriptor)
	{
		return false;
	}

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
		if (A.Category != B.Category)
		{
			return A.Category < B.Category;
		}
		return A.DisplayName < B.DisplayName;
	});
}

void FGaeaTerrainNodeDescriptorRegistry::Reset()
{
	FScopeLock Lock(&DescriptorRegistryMutex);
	DescriptorRegistry.Reset();
}
