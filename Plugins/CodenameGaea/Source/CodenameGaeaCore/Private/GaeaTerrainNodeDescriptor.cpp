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
		Parameter.Maximum = static_cast<double>(Maximum);
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
		Source.Description = TEXT("Uses the supplied terrain dataset as a graph source.");
		Source.Outputs.Add(TerrainPort(TEXT("Terrain")));
		DescriptorRegistry.Add(Source.Type, MoveTemp(Source));
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
