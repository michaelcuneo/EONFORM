#include "GaeaDenoiseNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor DenoiseAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainPortDescriptor DenoiseScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor DenoiseNumberParameter(
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

	FGaeaTerrainParameterDescriptor DenoiseBooleanParameter(
		FName Name,
		const TCHAR* DisplayName,
		bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	float DenoiseSampleClamped(const FGaeaScalarField& Field, int32 X, int32 Y)
	{
		const int32 SX = FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1);
		const int32 SY = FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1);
		return Field.AtInterior(SX, SY);
	}

	void DenoiseNeighborhoodStats(
		const FGaeaScalarField& Field,
		int32 X,
		int32 Y,
		float& OutMean,
		float& OutMedian,
		float& OutDeviation)
	{
		float Samples[9];
		int32 Count = 0;
		float Sum = 0.0f;
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const float V = DenoiseSampleClamped(Field, X + DX, Y + DY);
				Samples[Count++] = V;
				Sum += V;
			}
		}
		OutMean = Sum / 9.0f;
		Algo::Sort(Samples);
		OutMedian = Samples[4];
		OutDeviation = FMath::Abs(Field.AtInterior(X, Y) - OutMedian);
	}

	bool DenoiseProcessField(
		const FGaeaTerrainNode& Node,
		const FGaeaScalarField& Source,
		const FGaeaScalarField* AreaMask,
		FGaeaScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Denoise received an invalid scalar field.");
			return false;
		}
		if (AreaMask && (!AreaMask->IsValid() || AreaMask->Domain != Source.Domain))
		{
			Error = TEXT("Denoise Mask must use the same grid domain as Input.");
			return false;
		}

		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.5)), 0.0f, 1.0f);
		const float Despeckle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Despeckle"), 0.25)), 0.0f, 1.0f);
		const bool bApplyEnhancement = Node.GetBool(TEXT("ApplyEnhancement"), false);
		const bool bTargetStrayOnly = Node.GetBool(TEXT("TargetStrayPixelsOnly"), false);

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float Mean = 0.0f;
				float Median = 0.0f;
				float Deviation = 0.0f;
				DenoiseNeighborhoodStats(Source, X, Y, Mean, Median, Deviation);

				const float Center = Source.AtInterior(X, Y);
				const float LocalContrast = FMath::Abs(Center - Mean);
				const float StrayThreshold = FMath::Lerp(0.02f, 0.25f, 1.0f - Despeckle);
				const bool bIsStray = Deviation >= StrayThreshold || LocalContrast >= StrayThreshold;

				float Candidate = FMath::Lerp(Center, Median, Strength);
				if (bApplyEnhancement)
				{
					const float Detail = Center - Mean;
					Candidate += Detail * 0.25f * Strength;
				}
				if (bTargetStrayOnly && !bIsStray)
				{
					Candidate = Center;
				}

				const float MaskWeight = AreaMask ? FMath::Clamp(AreaMask->AtInterior(X, Y), 0.0f, 1.0f) : 1.0f;
				OutField.AtInterior(X, Y) = FMath::Lerp(Center, Candidate, MaskWeight);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateDenoiseNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Denoise requires a valid Input.");
			return false;
		}

		const FGaeaTerrainValue* const* MaskPtr = Inputs.Find(TEXT("Mask"));
		const FGaeaTerrainValue* MaskValue = MaskPtr ? *MaskPtr : nullptr;
		const FGaeaScalarField* AreaMask = nullptr;
		if (MaskValue)
		{
			if (MaskValue->Type != EGaeaTerrainValueType::ScalarField || !MaskValue->ScalarField.IsValid())
			{
				Error = TEXT("Denoise Mask input must be a valid scalar field.");
				return false;
			}
			AreaMask = &MaskValue->ScalarField;
		}

		if (Input->Type == EGaeaTerrainValueType::ScalarField)
		{
			FGaeaScalarField Result;
			if (!DenoiseProcessField(Node, Input->ScalarField, AreaMask, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EGaeaTerrainValueType::Terrain)
		{
			const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Denoise terrain input has no valid Height field.");
				return false;
			}
			FGaeaScalarField ResultHeight;
			if (!DenoiseProcessField(Node, *Height, AreaMask, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

			FGaeaTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Denoise could not publish its Height field.");
				return false;
			}

			FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
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

void RegisterGaeaDenoiseNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Denoise;
	Descriptor.DisplayName = TEXT("Denoise");
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Reduces unwanted noise and stray pixels while optionally enhancing retained detail.");
	Descriptor.Inputs.Add(DenoiseAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Inputs.Add(DenoiseScalarPort(TEXT("Mask"), TEXT("Mask")));
	Descriptor.Outputs.Add(DenoiseAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(DenoiseNumberParameter(TEXT("Strength"), TEXT("Strength"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(DenoiseNumberParameter(TEXT("Despeckle"), TEXT("Despeckle"), 0.25, 0.0, 1.0));
	Descriptor.Parameters.Add(DenoiseBooleanParameter(TEXT("ApplyEnhancement"), TEXT("Apply Enhancement"), false));
	Descriptor.Parameters.Add(DenoiseBooleanParameter(TEXT("TargetStrayPixelsOnly"), TEXT("Target Stray Pixels Only"), false));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Denoise, EvaluateDenoiseNode);
}
