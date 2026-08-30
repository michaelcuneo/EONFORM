#include "EonformSharpenNode.h"

#include "EonformRegionalFieldSampling.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor SharpenTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor SharpenNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FEonformTerrainParameterDescriptor SharpenNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("Edge"));
		Parameter.NameOptions.Add(TEXT("Frequency"));
		return Parameter;
	}

	bool SharpenHeightField(
		const FEonformTerrainNode& Node,
		const FEonformScalarField& Source,
		const FEonformTerrainEvaluationContext& Context,
		FEonformScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Sharpen received an invalid Height field.");
			return false;
		}

		const FName Method = Node.GetName(TEXT("Method"), TEXT("Edge"));
		if (Method != TEXT("Edge") && Method != TEXT("Frequency"))
		{
			Error = TEXT("Sharpen Method must be Edge or Frequency.");
			return false;
		}
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), 0.0f, 2.0f);
		const int32 RequiredBorder = EonformSharpenNode::RequiredBorderSamples(Node);
		if (Context.HasRegion() && Source.Domain.BorderSamples < RequiredBorder)
		{
			Error = TEXT("Regional Sharpen requires one dependency-border sample when Amount is non-zero.");
			return false;
		}
		if (RequiredBorder == 0)
		{
			OutField = Source;
			return true;
		}

		FEonformGridDomain WorldDomain;
		if (!EonformRegionalFieldSampling::ResolveWorldDomain(Source, Context, WorldDomain, Error)) return false;

		OutField = Source;
		const FIntPoint Storage = Source.Domain.GetStorageDimensions();
		for (int32 Y = 0; Y < Storage.Y; ++Y)
		{
			for (int32 X = 0; X < Storage.X; ++X)
			{
				const FIntPoint CenterCoord = EonformRegionalFieldSampling::ResolveStorageCoordinate(Source, X, Y, WorldDomain);
				float Sum = 0.0f;
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						Sum += EonformRegionalFieldSampling::SampleOffset(Source, CenterCoord, DX, DY, WorldDomain);
					}
				}
				const float Center = Source.AtStorage(CenterCoord.X, CenterCoord.Y);
				const float Blurred = Sum / 9.0f;
				const float Detail = Center - Blurred;
				const float Gain = Method == TEXT("Frequency") ? 1.5f : 1.0f;
				OutField.AtStorage(X, Y) = FMath::Clamp(Center + Detail * Amount * Gain, -1.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateSharpenNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Sharpen requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Sharpen terrain input has no valid Height field.");
			return false;
		}

		FEonformScalarField ResultHeight;
		if (!SharpenHeightField(Node, *Height, Context, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;

		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Sharpen could not publish its Height field.");
			return false;
		}

		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Sharpen produced an invalid terrain value.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterEonformSharpenNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Sharpen;
	Descriptor.DisplayName = TEXT("Sharpen");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Enhances terrain edges or high-frequency detail.");
	Descriptor.Inputs.Add(SharpenTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(SharpenTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(SharpenNameParameter(TEXT("Method"), TEXT("Method"), TEXT("Edge")));
	Descriptor.Parameters.Add(SharpenNumberParameter(TEXT("Amount"), TEXT("Amount"), 0.5, 0.0, 2.0));

	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Sharpen, EvaluateSharpenNode);
}
