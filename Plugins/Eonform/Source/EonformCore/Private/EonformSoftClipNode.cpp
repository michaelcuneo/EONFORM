#include "EonformSoftClipNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor SoftClipTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor SoftClipNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FEonformTerrainParameterDescriptor SoftClipNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("Above Threshold"));
		Parameter.NameOptions.Add(TEXT("Below Threshold"));
		return Parameter;
	}

	float SoftClipSmoothStep(float Edge0, float Edge1, float X)
	{
		if (FMath::IsNearlyEqual(Edge0, Edge1)) return X >= Edge1 ? 1.0f : 0.0f;
		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	bool SoftClipHeightField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("SoftClip received an invalid Height field.");
			return false;
		}

		const FName ClipMode = Node.GetName(TEXT("ClipMode"), TEXT("Above Threshold"));
		if (ClipMode != TEXT("Above Threshold") && ClipMode != TEXT("Below Threshold"))
		{
			Error = TEXT("SoftClip Clip Mode must be Above Threshold or Below Threshold.");
			return false;
		}

		const float Threshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Threshold"), 0.5)), 0.0f, 1.0f);
		const float Softness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Softness"), 0.1)), 0.0f, 1.0f);
		const float Clipping = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Clipping"), 1.0)), 0.0f, 1.0f);
		const float HalfSoftness = Softness * 0.5f;

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float SignedHeight = FMath::Clamp(Source.AtInterior(X, Y), -1.0f, 1.0f);
				const float Height01 = SignedHeight * 0.5f + 0.5f;
				float Clipped01 = Height01;
				if (ClipMode == TEXT("Above Threshold"))
				{
					const float Weight = 1.0f - SoftClipSmoothStep(Threshold - HalfSoftness, Threshold + HalfSoftness, Height01);
					const float Target = FMath::Min(Height01, Threshold);
					Clipped01 = FMath::Lerp(Target, Height01, Weight);
				}
				else
				{
					const float Weight = SoftClipSmoothStep(Threshold - HalfSoftness, Threshold + HalfSoftness, Height01);
					const float Target = FMath::Max(Height01, Threshold);
					Clipped01 = FMath::Lerp(Target, Height01, Weight);
				}
				const float Result01 = FMath::Lerp(Height01, Clipped01, Clipping);
				OutField.AtInterior(X, Y) = FMath::Clamp(Result01 * 2.0f - 1.0f, -1.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateSoftClipNode(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("SoftClip requires a valid terrain input 'Terrain'.");
			return false;
		}
		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("SoftClip terrain input has no valid Height field.");
			return false;
		}
		FEonformScalarField ResultHeight;
		if (!SoftClipHeightField(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("SoftClip could not publish its Height field.");
			return false;
		}
		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("SoftClip produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterEonformSoftClipNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::SoftClip;
	Descriptor.DisplayName = TEXT("SoftClip");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Softly clips terrain above or below a threshold with controllable transition softness and clipping strength.");
	Descriptor.Inputs.Add(SoftClipTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(SoftClipTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(SoftClipNameParameter(TEXT("ClipMode"), TEXT("Clip Mode"), TEXT("Above Threshold")));
	Descriptor.Parameters.Add(SoftClipNumberParameter(TEXT("Threshold"), TEXT("Threshold"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(SoftClipNumberParameter(TEXT("Softness"), TEXT("Softness"), 0.1, 0.0, 1.0));
	Descriptor.Parameters.Add(SoftClipNumberParameter(TEXT("Clipping"), TEXT("Clipping"), 1.0, 0.0, 1.0));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::SoftClip, EvaluateSoftClipNode);
}
