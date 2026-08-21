#include "GaeaDistanceNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor DistanceAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor DistanceNumberParameter(
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

	FGaeaTerrainParameterDescriptor DistanceBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	float DistanceNormalizedInput(float Value, bool bTerrain)
	{
		return bTerrain
			? FMath::Clamp(Value * 0.5f + 0.5f, 0.0f, 1.0f)
			: FMath::Clamp(Value, 0.0f, 1.0f);
	}

	void DistanceTransformInside(
		const TArray<uint8>& Inside,
		int32 Width,
		int32 Height,
		TArray<float>& Distances)
	{
		const float Far = static_cast<float>(Width + Height + 1);
		Distances.SetNumUninitialized(Width * Height);
		for (int32 Index = 0; Index < Distances.Num(); ++Index)
		{
			Distances[Index] = Inside[Index] ? Far : 0.0f;
		}

		const float Diagonal = 1.41421356237f;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = Y * Width + X;
				if (!Inside[Index]) continue;
				float D = Distances[Index];
				if (X > 0) D = FMath::Min(D, Distances[Index - 1] + 1.0f);
				if (Y > 0) D = FMath::Min(D, Distances[Index - Width] + 1.0f);
				if (X > 0 && Y > 0) D = FMath::Min(D, Distances[Index - Width - 1] + Diagonal);
				if (X + 1 < Width && Y > 0) D = FMath::Min(D, Distances[Index - Width + 1] + Diagonal);
				Distances[Index] = D;
			}
		}

		for (int32 Y = Height - 1; Y >= 0; --Y)
		{
			for (int32 X = Width - 1; X >= 0; --X)
			{
				const int32 Index = Y * Width + X;
				if (!Inside[Index]) continue;
				float D = Distances[Index];
				if (X + 1 < Width) D = FMath::Min(D, Distances[Index + 1] + 1.0f);
				if (Y + 1 < Height) D = FMath::Min(D, Distances[Index + Width] + 1.0f);
				if (X + 1 < Width && Y + 1 < Height) D = FMath::Min(D, Distances[Index + Width + 1] + Diagonal);
				if (X > 0 && Y + 1 < Height) D = FMath::Min(D, Distances[Index + Width - 1] + Diagonal);
				Distances[Index] = D;
			}
		}
	}

	bool DistanceProcessField(
		const FGaeaTerrainNode& Node,
		const FGaeaScalarField& Source,
		bool bTerrain,
		FGaeaScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Distance received an invalid field.");
			return false;
		}

		const int32 Width = Source.Domain.Dimensions.X;
		const int32 Height = Source.Domain.Dimensions.Y;
		const float Threshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Threshold"), 0.5)), 0.0f, 1.0f);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 0.25)), 0.001f, 1.0f);
		const bool bInvertInput = Node.GetBool(TEXT("InvertInput"), false);
		const bool bInvertOutput = Node.GetBool(TEXT("InvertOutput"), false);
		const bool bMultiplyByInput = Node.GetBool(TEXT("MultiplyByInput"), false);

		TArray<uint8> Inside;
		TArray<float> NormalizedInput;
		Inside.SetNumUninitialized(Width * Height);
		NormalizedInput.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = Y * Width + X;
				float Value = DistanceNormalizedInput(Source.AtInterior(X, Y), bTerrain);
				if (bInvertInput) Value = 1.0f - Value;
				NormalizedInput[Index] = Value;
				Inside[Index] = Value >= Threshold ? 1 : 0;
			}
		}

		TArray<float> Distances;
		DistanceTransformInside(Inside, Width, Height, Distances);
		const float MaxDistance = FMath::Max(1.0f, Falloff * 0.5f * static_cast<float>(FMath::Min(Width, Height)));

		OutField = Source;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = Y * Width + X;
				float Value = Inside[Index]
					? FMath::Clamp(Distances[Index] / MaxDistance, 0.0f, 1.0f)
					: 0.0f;
				if (bMultiplyByInput) Value *= NormalizedInput[Index];
				if (bInvertOutput) Value = 1.0f - Value;
				OutField.AtInterior(X, Y) = bTerrain ? Value * 2.0f - 1.0f : Value;
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateDistanceNode(
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
			Error = TEXT("Distance requires a valid Input.");
			return false;
		}

		if (Input->Type == EGaeaTerrainValueType::ScalarField)
		{
			FGaeaScalarField Result;
			if (!DistanceProcessField(Node, Input->ScalarField, false, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EGaeaTerrainValueType::Terrain)
		{
			const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Distance terrain input has no valid Height field.");
				return false;
			}

			FGaeaScalarField ResultHeight;
			if (!DistanceProcessField(Node, *Height, true, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;
			FGaeaTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Distance could not publish its Height field.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
			return true;
		}

		Error = TEXT("Distance received an unsupported input type.");
		return false;
	}
}

void RegisterGaeaDistanceNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Distance;
	Descriptor.DisplayName = TEXT("Distance");
	Descriptor.Category = TEXT("Profile");
	Descriptor.Description = TEXT("Creates a distance-based falloff profile from hard terrain or mask shapes.");
	Descriptor.Inputs.Add(DistanceAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(DistanceAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(DistanceNumberParameter(TEXT("Falloff"), TEXT("Falloff"), 0.25, 0.001, 1.0));
	Descriptor.Parameters.Add(DistanceNumberParameter(TEXT("Threshold"), TEXT("Threshold"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(DistanceBooleanParameter(TEXT("InvertInput"), TEXT("Invert input"), false));
	Descriptor.Parameters.Add(DistanceBooleanParameter(TEXT("InvertOutput"), TEXT("Invert output"), false));
	Descriptor.Parameters.Add(DistanceBooleanParameter(TEXT("MultiplyByInput"), TEXT("Multiply by input"), false));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Distance, EvaluateDistanceNode);
}
