#include "EonformTerraceNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "EonformTerrainTerrace.h"

namespace
{
	FEonformTerrainPortDescriptor TerraceTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor TerraceNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor TerraceIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Integer;
		Parameter.DefaultInteger = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = static_cast<double>(Maximum);
		return Parameter;
	}

	bool EvaluateTerraceNode(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Terraces requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
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

		FEonformScalarField Source01 = *Height;
		for (float& Value : Source01.Values) Value = FMath::Clamp(Value * 0.5f + 0.5f, 0.0f, 1.0f);

		FEonformScalarField Result01;
		if (!EonformTerrace::ApplyNormalized(Source01, TerraceCount, Uniformity, Steepness, Intensity, Seed, Result01, &Error)) return false;
		for (float& Value : Result01.Values) Value = FMath::Clamp(Value * 2.0f - 1.0f, -1.0f, 1.0f);
		Result01.Descriptor.Name = EonformTerrainFieldNames::Height;

		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(Result01)))
		{
			Error = TEXT("Terraces could not publish its Height field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		return true;
	}
}

bool EonformTerrace::ApplyNormalized(
	const FEonformScalarField& Source01,
	int32 TerraceCount,
	float Uniformity,
	float Steepness,
	float Intensity,
	int32 Seed,
	FEonformScalarField& OutField01,
	FString* OutError)
{
	return EonformTerrainProceduralOps::TerraceFidelity(
		Source01,
		TerraceCount,
		Uniformity,
		Steepness,
		Intensity,
		Seed,
		OutField01,
		OutError);
}

void RegisterEonformTerraceNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Terrace;
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
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Terrace, EvaluateTerraceNode);
}
