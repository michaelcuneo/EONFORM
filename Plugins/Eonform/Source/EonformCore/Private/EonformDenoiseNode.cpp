#include "EonformDenoiseNode.h"

#include "EonformRegionalFieldSampling.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor DenoiseAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor DenoiseNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FEonformTerrainParameterDescriptor DenoiseIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum)
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

	FEonformTerrainParameterDescriptor DenoiseNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("One Pass"));
		Parameter.NameOptions.Add(TEXT("Two Pass"));
		Parameter.NameOptions.Add(TEXT("Pixels"));
		return Parameter;
	}

	void DenoisePass(
		const FEonformScalarField& Source,
		FEonformScalarField& Dest,
		float Amount,
		bool bPixelsOnly,
		const FEonformGridDomain& WorldDomain)
	{
		const FIntPoint Storage = Source.Domain.GetStorageDimensions();
		for (int32 Y = 0; Y < Storage.Y; ++Y)
		{
			for (int32 X = 0; X < Storage.X; ++X)
			{
				const FIntPoint CenterCoord = EonformRegionalFieldSampling::ResolveStorageCoordinate(Source, X, Y, WorldDomain);
				float Samples[9];
				int32 Index = 0;
				float Mean = 0.0f;
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						const float Value = EonformRegionalFieldSampling::SampleOffset(Source, CenterCoord, DX, DY, WorldDomain);
						Samples[Index++] = Value;
						Mean += Value;
					}
				}
				Algo::Sort(Samples);
				Mean /= 9.0f;
				const float Center = Source.AtStorage(CenterCoord.X, CenterCoord.Y);
				const float Median = Samples[4];
				const bool bStray = FMath::Abs(Center - Median) > FMath::Max(0.01f, FMath::Abs(Center - Mean) * 0.75f);
				const float Target = bPixelsOnly && !bStray ? Center : Median;
				Dest.AtStorage(X, Y) = FMath::Lerp(Center, Target, Amount);
			}
		}
	}

	bool DenoiseProcessField(
		const FEonformTerrainNode& Node,
		const FEonformScalarField& Source,
		const FEonformTerrainEvaluationContext& Context,
		FEonformScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Denoise received an invalid scalar field.");
			return false;
		}

		const FName Type = Node.GetName(TEXT("Type"), TEXT("One Pass"));
		if (Type != TEXT("One Pass") && Type != TEXT("Two Pass") && Type != TEXT("Pixels"))
		{
			Error = TEXT("Denoise Type must be One Pass, Two Pass, or Pixels.");
			return false;
		}
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), 0.0f, 1.0f);
		const int32 Passes = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Passes"), 1)), 1, 32);
		const int32 PassMultiplier = Type == TEXT("Two Pass") ? 2 : 1;
		const bool bPixelsOnly = Type == TEXT("Pixels");
		const int32 RequiredBorder = EonformDenoiseNode::RequiredBorderSamples(Node);
		if (Context.HasRegion() && Source.Domain.BorderSamples < RequiredBorder)
		{
			Error = FString::Printf(
				TEXT("Regional Denoise requires %d dependency-border samples for this pass configuration; received %d."),
				RequiredBorder,
				Source.Domain.BorderSamples);
			return false;
		}

		FEonformGridDomain WorldDomain;
		if (!EonformRegionalFieldSampling::ResolveWorldDomain(Source, Context, WorldDomain, Error)) return false;

		FEonformScalarField Current = Source;
		FEonformScalarField Working = Source;
		for (int32 Pass = 0; Pass < Passes * PassMultiplier; ++Pass)
		{
			DenoisePass(Current, Working, Amount, bPixelsOnly, WorldDomain);
			Swap(Current, Working);
		}
		OutField = MoveTemp(Current);
		return OutField.IsValid();
	}

	bool EvaluateDenoiseNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Denoise requires a valid Input.");
			return false;
		}

		if (Input->Type == EEonformTerrainValueType::ScalarField)
		{
			FEonformScalarField Result;
			if (!DenoiseProcessField(Node, Input->ScalarField, Context, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EEonformTerrainValueType::Terrain)
		{
			const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Denoise terrain input has no valid Height field.");
				return false;
			}
			FEonformScalarField ResultHeight;
			if (!DenoiseProcessField(Node, *Height, Context, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;

			FEonformTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Denoise could not publish its Height field.");
				return false;
			}

			FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
			if (!Result.IsValid())
			{
				Error = TEXT("Denoise produced an invalid terrain value.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
			return true;
		}

		Error = TEXT("Denoise received an unsupported input type.");
		return false;
	}
}

void RegisterEonformDenoiseNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Denoise;
	Descriptor.DisplayName = TEXT("Denoise");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Removes random noise, spikes, and sharp pixel artifacts while preserving the overall form.");
	Descriptor.Inputs.Add(DenoiseAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(DenoiseAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(DenoiseNameParameter(TEXT("Type"), TEXT("Type"), TEXT("One Pass")));
	Descriptor.Parameters.Add(DenoiseNumberParameter(TEXT("Amount"), TEXT("Amount"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(DenoiseIntegerParameter(TEXT("Passes"), TEXT("Passes"), 1, 1, 32));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Denoise, EvaluateDenoiseNode);
}
