#include "GaeaShaperNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor ShaperTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor ShaperNumberParameter(
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

	FGaeaTerrainParameterDescriptor ShaperBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	float ShaperSampleClamped(const FGaeaScalarField& Field, int32 X, int32 Y)
	{
		const int32 SX = FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1);
		const int32 SY = FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1);
		return Field.AtInterior(SX, SY);
	}

	float ShaperNeighborhoodMean(const FGaeaScalarField& Field, int32 X, int32 Y, int32 Radius)
	{
		float Sum = 0.0f;
		int32 Count = 0;
		for (int32 DY = -Radius; DY <= Radius; ++DY)
		{
			for (int32 DX = -Radius; DX <= Radius; ++DX)
			{
				Sum += ShaperSampleClamped(Field, X + DX, Y + DY);
				++Count;
			}
		}
		return Count > 0 ? Sum / static_cast<float>(Count) : Field.AtInterior(X, Y);
	}

	bool ShaperHeightField(
		const FGaeaTerrainNode& Node,
		const FGaeaScalarField& Source,
		FGaeaScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Shaper received an invalid Height field.");
			return false;
		}

		const float Shape = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shape"), 0.0)), -1.0f, 1.0f);
		const float LocalEffect = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("LocalEffect"), 0.0)), 0.0f, 1.0f);
		const float LocalArea = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("LocalArea"), 0.5)), 0.0f, 1.0f);
		const bool bMaintainFineDetails = Node.GetBool(TEXT("MaintainFineDetails"), true);
		const float DetailSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("DetailSize"), 0.25)), 0.0f, 1.0f);
		const int32 DetailRadius = FMath::Clamp(1 + FMath::RoundToInt(DetailSize * 5.0f), 1, 6);

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float Center = Source.AtInterior(X, Y);
				const float Normalized = FMath::Clamp(Center * 0.5f + 0.5f, 0.0f, 1.0f);
				const float Exponent = Shape >= 0.0f
					? FMath::Lerp(1.0f, 0.35f, Shape)
					: FMath::Lerp(1.0f, 3.0f, -Shape);
				float Shaped = FMath::Pow(Normalized, Exponent) * 2.0f - 1.0f;

				if (LocalEffect > 0.0f)
				{
					const float LocalWeight = 1.0f - FMath::Clamp(FMath::Abs(Normalized - LocalArea) * 4.0f, 0.0f, 1.0f);
					Shaped = FMath::Lerp(Center, Shaped, LocalEffect * LocalWeight);
				}

				if (bMaintainFineDetails)
				{
					const float Mean = ShaperNeighborhoodMean(Source, X, Y, DetailRadius);
					const float Detail = Center - Mean;
					Shaped += Detail;
				}

				OutField.AtInterior(X, Y) = FMath::Clamp(Shaped, -1.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateShaperNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Shaper requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Shaper terrain input has no valid Height field.");
			return false;
		}

		FGaeaScalarField ResultHeight;
		if (!ShaperHeightField(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Shaper could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Shaper produced an invalid terrain value.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaShaperNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Shaper;
	Descriptor.DisplayName = TEXT("Shaper");
	Descriptor.Category = TEXT("Profile");
	Descriptor.Description = TEXT("Adds or removes terrain body while optionally preserving fine detail.");
	Descriptor.Inputs.Add(ShaperTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(ShaperTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ShaperNumberParameter(TEXT("Shape"), TEXT("Shape"), 0.0, -1.0, 1.0));
	Descriptor.Parameters.Add(ShaperNumberParameter(TEXT("LocalEffect"), TEXT("Local effect"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(ShaperNumberParameter(TEXT("LocalArea"), TEXT("Local area"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(ShaperBooleanParameter(TEXT("MaintainFineDetails"), TEXT("Maintain fine details"), true));
	Descriptor.Parameters.Add(ShaperNumberParameter(TEXT("DetailSize"), TEXT("Detail size"), 0.25, 0.0, 1.0));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Shaper, EvaluateShaperNode);
}
