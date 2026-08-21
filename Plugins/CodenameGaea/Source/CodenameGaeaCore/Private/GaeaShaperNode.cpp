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

	FGaeaTerrainParameterDescriptor ShaperNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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
		return Field.AtInterior(FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1), FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1));
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

	bool ShaperProcessHeight(const FGaeaTerrainNode& Node, const FGaeaScalarField& Source, FGaeaScalarField& OutField, FString& Error)
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
		const float Gamma = FMath::Pow(2.0f, -Shape * 2.0f);
		const int32 Radius = FMath::Clamp(1 + FMath::RoundToInt(DetailSize * 5.0f), 1, 6);

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float OriginalSigned = FMath::Clamp(Source.AtInterior(X, Y), -1.0f, 1.0f);
				const float Original = OriginalSigned * 0.5f + 0.5f;
				const float Shaped = FMath::Pow(FMath::Clamp(Original, 0.0f, 1.0f), Gamma);
				const float LocalDistance = FMath::Abs(Original - LocalArea) * 2.0f;
				const float LocalWeight = FMath::Lerp(1.0f, FMath::Clamp(1.0f - LocalDistance, 0.0f, 1.0f), LocalEffect);
				float Result = FMath::Lerp(Original, Shaped, LocalWeight);
				if (bMaintainFineDetails)
				{
					const float Mean = ShaperNeighborhoodMean(Source, X, Y, Radius) * 0.5f + 0.5f;
					const float FineDetail = Original - Mean;
					Result = FMath::Clamp(Result + FineDetail, 0.0f, 1.0f);
				}
				OutField.AtInterior(X, Y) = Result * 2.0f - 1.0f;
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateShaperNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
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
		if (!ShaperProcessHeight(Node, *Height, ResultHeight, Error)) return false;
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
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Adds or removes terrain body while optionally preserving fine detail.");
	Descriptor.Inputs.Add(ShaperTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(ShaperTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ShaperNumberParameter(TEXT("Shape"), TEXT("Shape"), 0.0, -1.0, 1.0));
	Descriptor.Parameters.Add(ShaperNumberParameter(TEXT("LocalEffect"), TEXT("Local Effect"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(ShaperNumberParameter(TEXT("LocalArea"), TEXT("Local Area"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(ShaperBooleanParameter(TEXT("MaintainFineDetails"), TEXT("Maintain Fine Details"), true));
	Descriptor.Parameters.Add(ShaperNumberParameter(TEXT("DetailSize"), TEXT("Detail Size"), 0.25, 0.0, 1.0));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Shaper, EvaluateShaperNode);
}
