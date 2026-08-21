#include "GaeaZeroBordersNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor ZeroBordersTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor ZeroBordersNumberParameter(
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

	FGaeaTerrainParameterDescriptor ZeroBordersIntegerParameter(
		FName Name,
		const TCHAR* DisplayName,
		int64 DefaultValue,
		int64 Minimum,
		int64 Maximum)
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

	FGaeaTerrainParameterDescriptor ZeroBordersBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor ZeroBordersNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions = { TEXT("Round"), TEXT("Square"), TEXT("Precise") };
		return Parameter;
	}

	float ZeroBordersDistanceToEdge(int32 X, int32 Y, int32 Width, int32 Height, FName Mode)
	{
		const float Left = static_cast<float>(X);
		const float Right = static_cast<float>(Width - 1 - X);
		const float Bottom = static_cast<float>(Y);
		const float Top = static_cast<float>(Height - 1 - Y);
		if (Mode == TEXT("Round"))
		{
			const float CenterX = 0.5f * static_cast<float>(Width - 1);
			const float CenterY = 0.5f * static_cast<float>(Height - 1);
			const float NX = CenterX > 0.0f ? FMath::Abs(static_cast<float>(X) - CenterX) / CenterX : 0.0f;
			const float NY = CenterY > 0.0f ? FMath::Abs(static_cast<float>(Y) - CenterY) / CenterY : 0.0f;
			const float Radius = FMath::Sqrt(NX * NX + NY * NY);
			return FMath::Max(0.0f, 1.0f - Radius) * FMath::Min(CenterX, CenterY);
		}
		if (Mode == TEXT("Precise"))
		{
			const float Horizontal = FMath::Min(Left, Right);
			const float Vertical = FMath::Min(Bottom, Top);
			return FMath::Min(Horizontal, Vertical);
		}
		return FMath::Min4(Left, Right, Bottom, Top);
	}

	void ZeroBordersBlurWeights(TArray<float>& Weights, int32 Width, int32 Height, int32 Iterations, int32 Radius)
	{
		if (Iterations <= 0 || Radius <= 0) return;
		TArray<float> Scratch;
		Scratch.SetNumUninitialized(Weights.Num());
		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					float Sum = 0.0f;
					int32 Count = 0;
					for (int32 DY = -Radius; DY <= Radius; ++DY)
					{
						for (int32 DX = -Radius; DX <= Radius; ++DX)
						{
							const int32 SX = FMath::Clamp(X + DX, 0, Width - 1);
							const int32 SY = FMath::Clamp(Y + DY, 0, Height - 1);
							Sum += Weights[SY * Width + SX];
							++Count;
						}
					}
					Scratch[Y * Width + X] = Count > 0 ? Sum / static_cast<float>(Count) : Weights[Y * Width + X];
				}
			}
			Swap(Weights, Scratch);
		}
	}

	bool ZeroBordersApplyToHeight(
		const FGaeaTerrainNode& Node,
		const FGaeaScalarField& Source,
		FGaeaScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Zero Borders received an invalid Height field.");
			return false;
		}

		const FName Mode = Node.GetName(TEXT("Mode"), TEXT("Round"));
		if (Mode != TEXT("Round") && Mode != TEXT("Square") && Mode != TEXT("Precise"))
		{
			Error = TEXT("Zero Borders Mode must be Round, Square, or Precise.");
			return false;
		}

		const int32 Width = Source.Domain.Dimensions.X;
		const int32 Height = Source.Domain.Dimensions.Y;
		const float MinDimension = static_cast<float>(FMath::Max(1, FMath::Min(Width, Height) - 1));
		const bool bAuto = Node.GetBool(TEXT("Auto"), true);
		const float MarginControl = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Margin"), 0.1)), 0.0f, 1.0f);
		const float FalloffControl = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 0.1)), 0.0f, 1.0f);
		const float Margin = bAuto ? MinDimension * 0.05f : MarginControl * MinDimension * 0.5f;
		const float Falloff = FMath::Max(1.0f, FalloffControl * MinDimension * 0.5f);
		const float BlurPower = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("BlurPower"), 0.0)), 0.0f, 1.0f);
		const int32 Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 1)), 1, 16);

		TArray<float> Weights;
		Weights.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float Distance = ZeroBordersDistanceToEdge(X, Y, Width, Height, Mode);
				const float Weight = FMath::Clamp((Distance - Margin) / Falloff, 0.0f, 1.0f);
				Weights[Y * Width + X] = Weight * Weight * (3.0f - 2.0f * Weight);
			}
		}

		if (BlurPower > 0.0f)
		{
			const int32 Radius = FMath::Clamp(FMath::RoundToInt(BlurPower * 4.0f), 1, 4);
			ZeroBordersBlurWeights(Weights, Width, Height, Iterations, Radius);
		}

		OutField = Source;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				OutField.AtInterior(X, Y) = Source.AtInterior(X, Y) * Weights[Y * Width + X];
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateZeroBordersNode(
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
			Error = TEXT("Zero Borders requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Zero Borders terrain input has no valid Height field.");
			return false;
		}

		FGaeaScalarField ResultHeight;
		if (!ZeroBordersApplyToHeight(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Zero Borders could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Zero Borders produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaZeroBordersNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::ZeroBorders;
	Descriptor.DisplayName = TEXT("Zero Borders");
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Fades terrain height to zero near the terrain borders.");
	Descriptor.Inputs.Add(ZeroBordersTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(ZeroBordersTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ZeroBordersNameParameter(TEXT("Mode"), TEXT("Mode"), TEXT("Round")));
	Descriptor.Parameters.Add(ZeroBordersNumberParameter(TEXT("Margin"), TEXT("Margin"), 0.1, 0.0, 1.0));
	Descriptor.Parameters.Add(ZeroBordersNumberParameter(TEXT("Falloff"), TEXT("Falloff"), 0.1, 0.0, 1.0));
	Descriptor.Parameters.Add(ZeroBordersBooleanParameter(TEXT("Auto"), TEXT("Auto"), true));
	Descriptor.Parameters.Add(ZeroBordersNumberParameter(TEXT("BlurPower"), TEXT("Blur Power"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(ZeroBordersIntegerParameter(TEXT("Iterations"), TEXT("Iterations"), 1, 1, 16));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::ZeroBorders, EvaluateZeroBordersNode);
}
