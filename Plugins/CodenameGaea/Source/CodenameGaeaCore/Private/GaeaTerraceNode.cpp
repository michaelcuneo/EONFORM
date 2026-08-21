#include "GaeaTerraceNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
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

	FGaeaTerrainParameterDescriptor TerraceBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	float TerraceHash01(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u;
		H += static_cast<uint32>(Y) * 668265263u;
		H += static_cast<uint32>(Seed) * 2246822519u;
		H = (H ^ (H >> 13u)) * 1274126177u;
		H ^= H >> 16u;
		return static_cast<float>(H & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
	}

	float TerraceShapeValue(float Value, int32 TerraceCount, float Uniformity, float Steepness, float SoftFalloff, float Variation)
	{
		const float Steps = static_cast<float>(FMath::Max(2, TerraceCount));
		const float Phase = Variation * (1.0f - Uniformity) / Steps;
		const float Scaled = FMath::Clamp(Value + Phase, 0.0f, 1.0f) * Steps;
		const float Base = FMath::FloorToFloat(Scaled);
		const float Fraction = FMath::Frac(Scaled);
		const float Sharpness = FMath::Lerp(1.0f, 12.0f, Steepness);
		float Edge = FMath::Pow(Fraction, Sharpness);
		if (SoftFalloff > 0.0f)
		{
			const float Smooth = Fraction * Fraction * (3.0f - 2.0f * Fraction);
			Edge = FMath::Lerp(Edge, Smooth, SoftFalloff);
		}
		return FMath::Clamp((Base + Edge) / Steps, 0.0f, 1.0f);
	}

	bool TerraceProcessHeight(const FGaeaTerrainNode& Node, const FGaeaScalarField& Source, FGaeaScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Terrace received an invalid Height field.");
			return false;
		}

		const int32 TerraceCount = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Terraces"), 12)), 2, 256);
		const float Uniformity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Uniformity"), 1.0)), 0.0f, 1.0f);
		const float Steepness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Steepness"), 0.65)), 0.0f, 1.0f);
		const float Intensity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Intensity"), 1.0)), 0.0f, 1.0f);
		const float SoftFalloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SoftFalloff"), 0.0)), 0.0f, 1.0f);
		const bool bReprocess = Node.GetBool(TEXT("Reprocess"), false);
		const float Process = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Process"), 1.0)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float OriginalSigned = FMath::Clamp(Source.AtInterior(X, Y), -1.0f, 1.0f);
				const float Original = OriginalSigned * 0.5f + 0.5f;
				const float Variation = TerraceHash01(X, Y, Seed) - 0.5f;
				float Terraced = TerraceShapeValue(Original, TerraceCount, Uniformity, Steepness, SoftFalloff, Variation);
				if (bReprocess)
				{
					const float Variation2 = TerraceHash01(X, Y, Seed ^ 0x5bd1e995) - 0.5f;
					Terraced = TerraceShapeValue(Terraced, TerraceCount, Uniformity, Steepness, SoftFalloff, Variation2);
				}
				const float Strength = FMath::Clamp(Intensity * Process, 0.0f, 1.0f);
				const float Result = FMath::Lerp(Original, Terraced, Strength);
				OutField.AtInterior(X, Y) = FMath::Clamp(Result * 2.0f - 1.0f, -1.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateTerraceNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Terrace requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Terrace terrain input has no valid Height field.");
			return false;
		}

		FGaeaScalarField ResultHeight;
		if (!TerraceProcessHeight(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Terrace could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Terrace produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaTerraceNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Terrace;
	Descriptor.DisplayName = TEXT("Terrace");
	Descriptor.Category = TEXT("Profile");
	Descriptor.Description = TEXT("Adds even stratification to terrain using configurable terrace count, uniformity, steepness, and intensity.");
	Descriptor.Inputs.Add(TerraceTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(TerraceTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(TerraceIntegerParameter(TEXT("Terraces"), TEXT("Terraces"), 12, 2, 256));
	Descriptor.Parameters.Add(TerraceNumberParameter(TEXT("Uniformity"), TEXT("Uniformity"), 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(TerraceNumberParameter(TEXT("Steepness"), TEXT("Steepness"), 0.65, 0.0, 1.0));
	Descriptor.Parameters.Add(TerraceNumberParameter(TEXT("Intensity"), TEXT("Intensity"), 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(TerraceNumberParameter(TEXT("SoftFalloff"), TEXT("Soft Falloff"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(TerraceBooleanParameter(TEXT("Reprocess"), TEXT("Reprocess"), false));
	Descriptor.Parameters.Add(TerraceNumberParameter(TEXT("Process"), TEXT("Process"), 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(TerraceIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Terrace, EvaluateTerraceNode);
}
