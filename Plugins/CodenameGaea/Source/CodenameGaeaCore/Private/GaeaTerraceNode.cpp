#include "GaeaTerraceNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainProceduralOps.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor TerraceTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor TerraceNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FGaeaTerrainParameterDescriptor TerraceIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum)
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

	bool EvaluateTerraceNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Terraces requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Terraces terrain input has no valid Height field.");
			return false;
		}

		const int32 TerraceCount = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Terraces"), 10)), 3, 256);
		const float Uniformity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Uniformity"), 0.6)), 0.0f, 1.0f);
		const float Steepness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Steepness"), 0.2)), 0.0f, 1.0f);
		const float Intensity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Intensity"), 1.0)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

		FGaeaScalarField Source01 = *Height;
		for (float& Value : Source01.Values)
		{
			Value = FMath::Clamp(Value * 0.5f + 0.5f, 0.0f, 1.0f);
		}

		FGaeaScalarField Result01;
		if (!GaeaTerrainProceduralOps::ApplyTerrace(Source01, TerraceCount, Uniformity, Steepness, Intensity, Seed, false, Result01, &Error)) return false;
		for (float& Value : Result01.Values)
		{
			Value = FMath::Clamp(Value * 2.0f - 1.0f, -1.0f, 1.0f);
		}
		Result01.Descriptor.Name = GaeaTerrainFieldNames::Height;

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(Result01)))
		{
			Error = TEXT("Terraces could not publish its Height field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		return true;
	}
}

void RegisterGaeaTerraceNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Terrace;
	Descriptor.DisplayName = TEXT("Terraces");
	Descriptor.Category = TEXT("Surface");
	Descriptor.Description = TEXT("Creates randomized terrain terrace levels with adjustable uniformity, steepness, intensity, and seed.");
	Descriptor.Inputs.Add(TerraceTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(TerraceTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(TerraceIntegerParameter(TEXT("Terraces"), TEXT("Terraces"), 10, 3, 256));
	Descriptor.Parameters.Add(TerraceNumberParameter(TEXT("Uniformity"), TEXT("Uniformity"), 0.6, 0.0, 1.0));
	Descriptor.Parameters.Add(TerraceNumberParameter(TEXT("Steepness"), TEXT("Steepness"), 0.2, 0.0, 1.0));
	Descriptor.Parameters.Add(TerraceNumberParameter(TEXT("Intensity"), TEXT("Intensity"), 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(TerraceIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Terrace, EvaluateTerraceNode);
}
