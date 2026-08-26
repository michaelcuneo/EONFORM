#include "EonformDistanceNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor DistanceAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor DistanceNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FEonformTerrainParameterDescriptor DistanceIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum)
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

	FEonformTerrainParameterDescriptor DistanceBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor DistanceNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		return Parameter;
	}

	float DistanceHash01(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u;
		H += static_cast<uint32>(Y) * 668265263u;
		H += static_cast<uint32>(Seed) * 2246822519u;
		H = (H ^ (H >> 13u)) * 1274126177u;
		H ^= H >> 16u;
		return static_cast<float>(H & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
	}

	float DistanceNormalizedInput(float Value, bool bTerrain)
	{
		return bTerrain ? FMath::Clamp(Value * 0.5f + 0.5f, 0.0f, 1.0f) : FMath::Clamp(Value, 0.0f, 1.0f);
	}

	void DistanceTransformInside(const TArray<uint8>& Inside, int32 Width, int32 Height, TArray<float>& Distances)
	{
		const float Far = static_cast<float>(Width + Height + 1);
		Distances.SetNumUninitialized(Width * Height);
		for (int32 Index = 0; Index < Distances.Num(); ++Index) Distances[Index] = Inside[Index] ? Far : 0.0f;

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

	bool DistanceProcessField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool bTerrain, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Distance received an invalid field.");
			return false;
		}

		const FName Method = Node.GetName(TEXT("Method"), TEXT("Classic"));
		const FName Mode = Node.GetName(TEXT("Mode"), TEXT("Asterisk"));
		if (Method != TEXT("Classic") && Method != TEXT("RT"))
		{
			Error = TEXT("Distance Method must be Classic or RT.");
			return false;
		}
		if (Mode != TEXT("Asterisk") && Mode != TEXT("Pyramid"))
		{
			Error = TEXT("Distance Mode must be Asterisk or Pyramid.");
			return false;
		}

		const int32 Width = Source.Domain.Dimensions.X;
		const int32 Height = Source.Domain.Dimensions.Y;
		const float Directions = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Directions"), 1.0)), 0.0f, 1.0f);
		const float Skew = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Skew"), 0.0)), -1.0f, 1.0f);
		const float Angle = static_cast<float>(Node.GetNumber(TEXT("Angle"), 0.0));
		const float AngularJitter = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("AngularJitter"), 0.0)), 0.0f, 1.0f);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 0.25)), 0.001f, 1.0f);
		const float Threshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Threshold"), 0.5)), 0.0f, 1.0f);
		const float FalloffJitter = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("FalloffJitter"), 0.0)), 0.0f, 1.0f);
		const bool bInvertInput = Node.GetBool(TEXT("InvertInput"), false);
		const bool bInvertOutput = Node.GetBool(TEXT("InvertOutput"), false);
		const bool bMultiplyByInput = Node.GetBool(TEXT("MultiplyByInput"), false);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

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
		const float BaseMaxDistance = FMath::Max(1.0f, Falloff * 0.5f * static_cast<float>(FMath::Min(Width, Height)));
		const float Radians = FMath::DegreesToRadians(Angle);
		const FVector2D Axis(FMath::Cos(Radians), FMath::Sin(Radians));
		const FVector2D Perp(-Axis.Y, Axis.X);
		const float CenterX = 0.5f * static_cast<float>(Width - 1);
		const float CenterY = 0.5f * static_cast<float>(Height - 1);

		OutField = Source;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = Y * Width + X;
				const float Jitter = DistanceHash01(X, Y, Seed) - 0.5f;
				const float LocalFalloff = BaseMaxDistance * FMath::Max(0.1f, 1.0f + Jitter * 2.0f * FalloffJitter);
				float Value = Inside[Index] ? FMath::Clamp(Distances[Index] / LocalFalloff, 0.0f, 1.0f) : 0.0f;

				const FVector2D P(static_cast<float>(X) - CenterX, static_cast<float>(Y) - CenterY);
				const float Along = FVector2D::DotProduct(P, Axis);
				const float Across = FVector2D::DotProduct(P, Perp);
				const float Directional = Mode == TEXT("Pyramid")
					? FMath::Abs(Along) + FMath::Abs(Across)
					: FMath::Max(FMath::Abs(Along), FMath::Abs(Across));
				const float DirectionScale = 1.0f + Skew * (Along / FMath::Max(1.0f, static_cast<float>(FMath::Max(Width, Height))))
					+ AngularJitter * Jitter * 0.25f;
				Value *= FMath::Lerp(1.0f, FMath::Clamp(1.0f - Directional / FMath::Max(1.0f, static_cast<float>(FMath::Max(Width, Height))), 0.0f, 1.0f), Directions * 0.25f);
				Value = FMath::Clamp(Value * FMath::Max(0.0f, DirectionScale), 0.0f, 1.0f);
				if (Method == TEXT("RT")) Value = Value * Value * (3.0f - 2.0f * Value);
				if (bMultiplyByInput) Value *= NormalizedInput[Index];
				if (bInvertOutput) Value = 1.0f - Value;
				OutField.AtInterior(X, Y) = bTerrain ? Value * 2.0f - 1.0f : Value;
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateDistanceNode(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Distance requires a valid Input.");
			return false;
		}

		if (Input->Type == EEonformTerrainValueType::ScalarField)
		{
			FEonformScalarField Result;
			if (!DistanceProcessField(Node, Input->ScalarField, false, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EEonformTerrainValueType::Terrain)
		{
			const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Distance terrain input has no valid Height field.");
				return false;
			}
			FEonformScalarField ResultHeight;
			if (!DistanceProcessField(Node, *Height, true, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;
			FEonformTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Distance could not publish its Height field.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
			return true;
		}

		Error = TEXT("Distance received an unsupported input type.");
		return false;
	}
}

void RegisterEonformDistanceNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Distance;
	Descriptor.DisplayName = TEXT("Distance");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Creates directional distance profiles from hard terrain or mask shapes.");
	Descriptor.Inputs.Add(DistanceAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(DistanceAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(DistanceNameParameter(TEXT("Method"), TEXT("Method"), TEXT("Classic"), { TEXT("Classic"), TEXT("RT") }));
	Descriptor.Parameters.Add(DistanceNameParameter(TEXT("Mode"), TEXT("Mode"), TEXT("Asterisk"), { TEXT("Asterisk"), TEXT("Pyramid") }));
	Descriptor.Parameters.Add(DistanceNumberParameter(TEXT("Directions"), TEXT("Directions"), 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(DistanceNumberParameter(TEXT("Skew"), TEXT("Skew"), 0.0, -1.0, 1.0));
	Descriptor.Parameters.Add(DistanceNumberParameter(TEXT("Angle"), TEXT("Angle"), 0.0, -360.0, 360.0));
	Descriptor.Parameters.Add(DistanceNumberParameter(TEXT("AngularJitter"), TEXT("Angular Jitter"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(DistanceNumberParameter(TEXT("Falloff"), TEXT("Falloff"), 0.25, 0.001, 1.0));
	Descriptor.Parameters.Add(DistanceNumberParameter(TEXT("Threshold"), TEXT("Threshold"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(DistanceNumberParameter(TEXT("FalloffJitter"), TEXT("Falloff Jitter"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(DistanceBooleanParameter(TEXT("InvertInput"), TEXT("Invert Input"), false));
	Descriptor.Parameters.Add(DistanceBooleanParameter(TEXT("InvertOutput"), TEXT("Invert Output"), false));
	Descriptor.Parameters.Add(DistanceBooleanParameter(TEXT("MultiplyByInput"), TEXT("Multiply by Input"), false));
	Descriptor.Parameters.Add(DistanceIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));

	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Distance, EvaluateDistanceNode);
}
