#include "EonformModifyFoundationNodes.h"

#include "Algo/Sort.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor AnyPort(FName Name, const TCHAR* Label)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		Port.DisplayName = Label;
		return Port;
	}

	FEonformTerrainParameterDescriptor NumberParam(FName Name, const TCHAR* Label, double DefaultValue, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Number;
		P.DefaultNumber = DefaultValue;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		return P;
	}

	FEonformTerrainParameterDescriptor IntegerParam(FName Name, const TCHAR* Label, int64 DefaultValue, int64 Min, int64 Max)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Integer;
		P.DefaultInteger = DefaultValue;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = static_cast<double>(Max);
		return P;
	}

	FEonformTerrainParameterDescriptor BoolParam(FName Name, const TCHAR* Label, bool DefaultValue)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Boolean;
		P.DefaultBoolean = DefaultValue;
		return P;
	}

	FEonformTerrainParameterDescriptor NameParam(FName Name, const TCHAR* Label, FName DefaultValue, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Name;
		P.DefaultName = DefaultValue;
		for (const FName Option : Options) P.NameOptions.Add(Option);
		return P;
	}

	float To01(float Value, bool bTerrain)
	{
		return bTerrain ? FMath::Clamp(Value * 0.5f + 0.5f, 0.0f, 1.0f) : FMath::Clamp(Value, 0.0f, 1.0f);
	}

	float From01(float Value, bool bTerrain)
	{
		const float V = FMath::Clamp(Value, 0.0f, 1.0f);
		return bTerrain ? V * 2.0f - 1.0f : V;
	}

	float SampleClamped(const FEonformScalarField& Field, int32 X, int32 Y)
	{
		return Field.AtInterior(
			FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1),
			FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1));
	}

	using FFieldProcessor = TFunction<bool(const FEonformTerrainNode&, const FEonformScalarField&, bool, FEonformScalarField&, FString&)>;

	bool EvaluateAnyFieldNode(
		const TCHAR* NodeLabel,
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error,
		const FFieldProcessor& Processor)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = FString::Printf(TEXT("%s requires a valid Input."), NodeLabel);
			return false;
		}

		if (Input->Type == EEonformTerrainValueType::ScalarField)
		{
			FEonformScalarField Result;
			if (!Processor(Node, Input->ScalarField, false, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EEonformTerrainValueType::Terrain)
		{
			const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = FString::Printf(TEXT("%s terrain input has no valid Height field."), NodeLabel);
				return false;
			}
			FEonformScalarField ResultHeight;
			if (!Processor(Node, *Height, true, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;
			FEonformTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = FString::Printf(TEXT("%s could not publish its Height field."), NodeLabel);
				return false;
			}
			FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
			if (!Result.IsValid())
			{
				Error = FString::Printf(TEXT("%s produced an invalid terrain value."), NodeLabel);
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
			return true;
		}

		Error = FString::Printf(TEXT("%s received an unsupported input type."), NodeLabel);
		return false;
	}

	void FieldMinMax(const FEonformScalarField& Source, float& OutMin, float& OutMax)
	{
		OutMin = TNumericLimits<float>::Max();
		OutMax = TNumericLimits<float>::Lowest();
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float V = Source.AtInterior(X, Y);
				OutMin = FMath::Min(OutMin, V);
				OutMax = FMath::Max(OutMax, V);
			}
		}
	}

	bool AdjustField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool bTerrain, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Adjust received an invalid field."); return false; }
		OutField = Source;

		float SourceMin = 0.0f;
		float SourceMax = 1.0f;
		FieldMinMax(Source, SourceMin, SourceMax);
		const float Range = FMath::Max(SourceMax - SourceMin, UE_SMALL_NUMBER);

		const float Multiply = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Multiply"), 1.0)), 0.0f, 8.0f);
		const float Shape = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shaper"), 0.0)), -1.0f, 1.0f);
		const float ClampMin = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ClampMin"), 0.0)), 0.0f, 1.0f);
		const float ClampMax = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ClampMax"), 1.0)), ClampMin, 1.0f);
		const float ClipMin = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ClipMin"), 0.0)), 0.0f, 1.0f);
		const float ClipMax = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ClipMax"), 1.0)), ClipMin, 1.0f);
		const float Drop = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Drop"), 0.0)), 0.0f, 1.0f);
		const bool bAutolevel = Node.GetBool(TEXT("Autolevel"), false);
		const bool bEqualize = Node.GetBool(TEXT("Equalize"), false);
		const bool bStrong = Node.GetBool(TEXT("Strong"), false);
		const bool bInvert = Node.GetBool(TEXT("Invert"), false);
		const float Gamma = FMath::Pow(2.0f, -Shape * 2.0f);

		TArray<float> Values;
		Values.SetNumUninitialized(Source.Domain.Dimensions.X * Source.Domain.Dimensions.Y);
		int32 I = 0;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float V = To01(Source.AtInterior(X, Y), bTerrain);
				if (bAutolevel) V = FMath::Clamp((Source.AtInterior(X, Y) - SourceMin) / Range, 0.0f, 1.0f);
				V = FMath::Clamp(V * Multiply, 0.0f, 1.0f);
				V = FMath::Pow(V, Gamma);
				V = FMath::Clamp(V, ClampMin, ClampMax);
				if (V < ClipMin || V > ClipMax) V = 0.0f;
				V = FMath::Max(0.0f, V - Drop);
				if (bStrong) V = FMath::Clamp((V - 0.5f) * 1.5f + 0.5f, 0.0f, 1.0f);
				if (bInvert) V = 1.0f - V;
				Values[I++] = V;
			}
		}

		if (bEqualize && Values.Num() > 1)
		{
			TArray<float> Sorted = Values;
			Algo::Sort(Sorted);
			for (float& V : Values)
			{
				const int32 Rank = Algo::LowerBound(Sorted, V);
				V = static_cast<float>(Rank) / static_cast<float>(Sorted.Num() - 1);
			}
		}

		I = 0;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X) OutField.AtInterior(X, Y) = From01(Values[I++], bTerrain);
		}
		return true;
	}

	bool KernelContains(FName Shape, int32 DX, int32 DY, int32 Radius, float DirectionDegrees)
	{
		if (Radius <= 0) return DX == 0 && DY == 0;
		if (Shape == TEXT("Disk")) return DX * DX + DY * DY <= Radius * Radius;
		if (Shape == TEXT("Asterisk")) return DX == 0 || DY == 0 || FMath::Abs(DX) == FMath::Abs(DY);
		if (Shape == TEXT("Line"))
		{
			const float R = FMath::DegreesToRadians(DirectionDegrees);
			const FVector2D Axis(FMath::Cos(R), FMath::Sin(R));
			const FVector2D P(static_cast<float>(DX), static_cast<float>(DY));
			return FMath::Abs(P.X * (-Axis.Y) + P.Y * Axis.X) <= 0.6f;
		}
		if (Shape == TEXT("Corner")) return DX >= 0 && DY >= 0;
		return true;
	}

	bool ApertureField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Aperture received an invalid field."); return false; }
		const FName Mode = Node.GetName(TEXT("Mode"), TEXT("Dilation"));
		const FName Shape = Node.GetName(TEXT("Shape"), TEXT("Disk"));
		if (Mode != TEXT("Dilation") && Mode != TEXT("Erosion")) { Error = TEXT("Aperture Mode must be Dilation or Erosion."); return false; }
		const int32 Radius = FMath::Clamp(FMath::RoundToInt(Node.GetNumber(TEXT("Size"), 2.0)), 1, 32);
		const float Direction = static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0));
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float Best = Mode == TEXT("Dilation") ? TNumericLimits<float>::Lowest() : TNumericLimits<float>::Max();
				for (int32 DY = -Radius; DY <= Radius; ++DY)
				{
					for (int32 DX = -Radius; DX <= Radius; ++DX)
					{
						if (!KernelContains(Shape, DX, DY, Radius, Direction)) continue;
						const float V = SampleClamped(Source, X + DX, Y + DY);
						Best = Mode == TEXT("Dilation") ? FMath::Max(Best, V) : FMath::Min(Best, V);
					}
				}
				OutField.AtInterior(X, Y) = Best;
			}
		}
		return true;
	}

	bool BlobRemoverField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool bTerrain, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("BlobRemover received an invalid field."); return false; }
		const int32 W = Source.Domain.Dimensions.X;
		const int32 H = Source.Domain.Dimensions.Y;
		const float Threshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Range"), 0.5)), 0.0f, 1.0f);
		const int32 Connectivity = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Connectivity"), 8)), 4, 8);
		const int32 Quantization = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Quantization"), 16)), 1, 128);
		const bool bQuantize = Node.GetBool(TEXT("EnforceQuantization"), false);
		const int32 MinArea = FMath::Max(2, FMath::RoundToInt((1.0f - Threshold) * 0.0025f * static_cast<float>(W * H)));
		TArray<uint8> Active;
		TArray<uint8> Visited;
		Active.SetNumZeroed(W * H);
		Visited.SetNumZeroed(W * H);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				float V = To01(Source.AtInterior(X, Y), bTerrain);
				if (bQuantize) V = FMath::RoundToFloat(V * Quantization) / static_cast<float>(Quantization);
				Active[Y * W + X] = V >= Threshold ? 1 : 0;
			}
		}
		OutField = Source;
		const int32 DX8[8] = { 1,-1,0,0,1,1,-1,-1 };
		const int32 DY8[8] = { 0,0,1,-1,1,-1,1,-1 };
		for (int32 Start = 0; Start < W * H; ++Start)
		{
			if (!Active[Start] || Visited[Start]) continue;
			TArray<int32> Stack;
			TArray<int32> Component;
			Stack.Add(Start);
			Visited[Start] = 1;
			while (!Stack.IsEmpty())
			{
				const int32 Index = Stack.Pop(EAllowShrinking::No);
				Component.Add(Index);
				const int32 X = Index % W;
				const int32 Y = Index / W;
				for (int32 N = 0; N < Connectivity; ++N)
				{
					const int32 NX = X + DX8[N];
					const int32 NY = Y + DY8[N];
					if (NX < 0 || NY < 0 || NX >= W || NY >= H) continue;
					const int32 NI = NY * W + NX;
					if (Active[NI] && !Visited[NI]) { Visited[NI] = 1; Stack.Add(NI); }
				}
			}
			if (Component.Num() < MinArea)
			{
				for (const int32 Index : Component) OutField.AtInterior(Index % W, Index / W) = From01(0.0f, bTerrain);
			}
		}
		return true;
	}

	bool ClipField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool bTerrain, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Clip received an invalid field."); return false; }
		float Min = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Min"), 0.0)), 0.0f, 1.0f);
		float Max = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Max"), 1.0)), Min, 1.0f);
		if (Node.GetBool(TEXT("AutoClip"), false))
		{
			TArray<float> Sorted;
			Sorted.Reserve(Source.Domain.Dimensions.X * Source.Domain.Dimensions.Y);
			for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X) Sorted.Add(To01(Source.AtInterior(X, Y), bTerrain));
			Algo::Sort(Sorted);
			if (Sorted.Num() > 4) { Min = Sorted[FMath::FloorToInt((Sorted.Num() - 1) * 0.01f)]; Max = Sorted[FMath::FloorToInt((Sorted.Num() - 1) * 0.99f)]; }
		}
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float V = To01(Source.AtInterior(X, Y), bTerrain);
				OutField.AtInterior(X, Y) = From01((V >= Min && V <= Max) ? V : 0.0f, bTerrain);
			}
		}
		return true;
	}

	bool DeflateField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Deflate received an invalid field."); return false; }
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), 0.0f, 1.0f);
		const int32 Radius = FMath::Clamp(1 + FMath::RoundToInt(Amount * 12.0f), 1, 16);
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float Mean = 0.0f; int32 Count = 0;
				for (int32 DY = -Radius; DY <= Radius; ++DY) for (int32 DX = -Radius; DX <= Radius; ++DX) { Mean += SampleClamped(Source, X + DX, Y + DY); ++Count; }
				Mean /= FMath::Max(Count, 1);
				const float Detail = Source.AtInterior(X, Y) - Mean;
				OutField.AtInterior(X, Y) = FMath::Clamp(FMath::Lerp(Source.AtInterior(X, Y), Detail, Amount), -1.0f, 1.0f);
			}
		}
		return true;
	}

	bool DilateField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Dilate received an invalid field."); return false; }
		const int32 Radius = FMath::Clamp(FMath::RoundToInt(Node.GetNumber(TEXT("Size"), 1.0)), 1, 16);
		const int32 Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 1)), 1, 16);
		const FName Kernel = Node.GetName(TEXT("Kernel"), TEXT("Rectangle"));
		const bool bInvert = Node.GetBool(TEXT("Invert"), false);
		FEonformScalarField Current = Source;
		FEonformScalarField Next = Source;
		for (int32 Pass = 0; Pass < Iterations; ++Pass)
		{
			for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
				{
					float Best = bInvert ? TNumericLimits<float>::Max() : TNumericLimits<float>::Lowest();
					for (int32 DY = -Radius; DY <= Radius; ++DY)
					{
						for (int32 DX = -Radius; DX <= Radius; ++DX)
						{
							bool bUse = Kernel == TEXT("Rectangle") || (Kernel == TEXT("Cross") && (DX == 0 || DY == 0)) || (Kernel == TEXT("Line") && DY == 0) || (Kernel == TEXT("Line Vertical") && DX == 0);
							if (!bUse) continue;
							const float V = SampleClamped(Current, X + DX, Y + DY);
							Best = bInvert ? FMath::Min(Best, V) : FMath::Max(Best, V);
						}
					}
					Next.AtInterior(X, Y) = Best;
				}
			}
			Swap(Current, Next);
		}
		OutField = MoveTemp(Current);
		return true;
	}

	bool EqualizeField(const FEonformTerrainNode&, const FEonformScalarField& Source, bool bTerrain, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Equalize received an invalid field."); return false; }
		TArray<float> Sorted;
		Sorted.Reserve(Source.Domain.Dimensions.X * Source.Domain.Dimensions.Y);
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X) Sorted.Add(To01(Source.AtInterior(X, Y), bTerrain));
		Algo::Sort(Sorted);
		OutField = Source;
		const float Denom = static_cast<float>(FMath::Max(1, Sorted.Num() - 1));
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float V = To01(Source.AtInterior(X, Y), bTerrain);
				OutField.AtInterior(X, Y) = From01(static_cast<float>(Algo::LowerBound(Sorted, V)) / Denom, bTerrain);
			}
		}
		return true;
	}

	bool ExtendField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool bTerrain, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Extend received an invalid field."); return false; }
		const float Value = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Value"), 1.0)), 0.0f, 4.0f);
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float V = To01(Source.AtInterior(X, Y), bTerrain);
				OutField.AtInterior(X, Y) = From01(FMath::Clamp(V * Value, 0.0f, 1.0f), bTerrain);
			}
		}
		return true;
	}

	bool MedianField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Median received an invalid field."); return false; }
		const int32 Radius = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Radius"), 1)), 1, 8);
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 1.0)), 0.0f, 1.0f);
		OutField = Source;
		TArray<float> Samples;
		Samples.Reserve((Radius * 2 + 1) * (Radius * 2 + 1));
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				Samples.Reset();
				for (int32 DY = -Radius; DY <= Radius; ++DY) for (int32 DX = -Radius; DX <= Radius; ++DX) Samples.Add(SampleClamped(Source, X + DX, Y + DY));
				Algo::Sort(Samples);
				const float Median = Samples[Samples.Num() / 2];
				OutField.AtInterior(X, Y) = FMath::Lerp(Source.AtInterior(X, Y), Median, Amount);
			}
		}
		return true;
	}

	void RegisterNode(FName Type, const TCHAR* DisplayName, const TCHAR* Description, TArray<FEonformTerrainParameterDescriptor>&& Parameters, FEonformTerrainNodeEvaluator Evaluator)
	{
		FEonformTerrainNodeDescriptor D;
		D.Type = Type;
		D.DisplayName = DisplayName;
		D.Category = TEXT("Modify");
		D.Description = Description;
		D.Inputs.Add(AnyPort(TEXT("Input"), TEXT("Input")));
		D.Outputs.Add(AnyPort(TEXT("Out"), TEXT("Out")));
		D.Parameters = MoveTemp(Parameters);
		FEonformTerrainNodeDescriptorRegistry::Register(D);
		FEonformTerrainNodeRegistry::Register(Type, MoveTemp(Evaluator));
	}
}

void RegisterEonformAdjustNode()
{
	TArray<FEonformTerrainParameterDescriptor> P;
	P.Add(NumberParam(TEXT("Multiply"), TEXT("Multiply"), 1.0, 0.0, 8.0));
	P.Add(NumberParam(TEXT("Shaper"), TEXT("Shaper"), 0.0, -1.0, 1.0));
	P.Add(NumberParam(TEXT("ClampMin"), TEXT("Clamp Min"), 0.0, 0.0, 1.0));
	P.Add(NumberParam(TEXT("ClampMax"), TEXT("Clamp Max"), 1.0, 0.0, 1.0));
	P.Add(NumberParam(TEXT("ClipMin"), TEXT("Clip Min"), 0.0, 0.0, 1.0));
	P.Add(NumberParam(TEXT("ClipMax"), TEXT("Clip Max"), 1.0, 0.0, 1.0));
	P.Add(NumberParam(TEXT("Drop"), TEXT("Drop"), 0.0, 0.0, 1.0));
	P.Add(BoolParam(TEXT("Autolevel"), TEXT("Autolevel"), false));
	P.Add(BoolParam(TEXT("Equalize"), TEXT("Equalize"), false));
	P.Add(BoolParam(TEXT("Strong"), TEXT("Strong"), false));
	P.Add(BoolParam(TEXT("Invert"), TEXT("Invert"), false));
	RegisterNode(EonformTerrainNodeTypes::Adjust, TEXT("Adjust"), TEXT("Combines common value remapping operations in one modifier node."), MoveTemp(P), [](const FEonformTerrainNode& N, const FEonformTerrainNodeInputs& I, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& O, FString& E){ return EvaluateAnyFieldNode(TEXT("Adjust"), N, I, O, E, AdjustField); });
}

void RegisterEonformApertureNode()
{
	TArray<FEonformTerrainParameterDescriptor> P;
	P.Add(NameParam(TEXT("Mode"), TEXT("Mode"), TEXT("Dilation"), { TEXT("Dilation"), TEXT("Erosion") }));
	P.Add(NumberParam(TEXT("Size"), TEXT("Size"), 2.0, 1.0, 32.0));
	P.Add(NameParam(TEXT("Shape"), TEXT("Shape"), TEXT("Disk"), { TEXT("Disk"), TEXT("Polygon"), TEXT("Asterisk"), TEXT("Line"), TEXT("Corner") }));
	P.Add(IntegerParam(TEXT("Vertices"), TEXT("Vertices Count"), 6, 3, 16));
	P.Add(NumberParam(TEXT("CornerAngle"), TEXT("Corner Angle"), 90.0, 0.0, 180.0));
	P.Add(NumberParam(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0));
	P.Add(BoolParam(TEXT("Antialiased"), TEXT("Antialiased"), true));
	RegisterNode(EonformTerrainNodeTypes::Aperture, TEXT("Aperture"), TEXT("Expands or compacts features using configurable morphological kernels."), MoveTemp(P), [](const FEonformTerrainNode& N, const FEonformTerrainNodeInputs& I, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& O, FString& E){ return EvaluateAnyFieldNode(TEXT("Aperture"), N, I, O, E, ApertureField); });
}

void RegisterEonformBlobRemoverNode()
{
	TArray<FEonformTerrainParameterDescriptor> P;
	P.Add(NumberParam(TEXT("Range"), TEXT("Range"), 0.5, 0.0, 1.0));
	P.Add(IntegerParam(TEXT("Connectivity"), TEXT("Connectivity"), 8, 4, 8));
	P.Add(BoolParam(TEXT("EnforceQuantization"), TEXT("Enforce Quantization"), false));
	P.Add(IntegerParam(TEXT("Quantization"), TEXT("Quantization"), 16, 1, 128));
	RegisterNode(EonformTerrainNodeTypes::BlobRemover, TEXT("BlobRemover"), TEXT("Removes small disconnected fragments while preserving larger shapes."), MoveTemp(P), [](const FEonformTerrainNode& N, const FEonformTerrainNodeInputs& I, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& O, FString& E){ return EvaluateAnyFieldNode(TEXT("BlobRemover"), N, I, O, E, BlobRemoverField); });
}

void RegisterEonformClipNode()
{
	TArray<FEonformTerrainParameterDescriptor> P;
	P.Add(NumberParam(TEXT("Min"), TEXT("Min"), 0.0, 0.0, 1.0));
	P.Add(NumberParam(TEXT("Max"), TEXT("Max"), 1.0, 0.0, 1.0));
	P.Add(BoolParam(TEXT("AutoClip"), TEXT("Auto Clip"), false));
	RegisterNode(EonformTerrainNodeTypes::Clip, TEXT("Clip"), TEXT("Cuts values outside a selected range, with optional automatic percentile clipping."), MoveTemp(P), [](const FEonformTerrainNode& N, const FEonformTerrainNodeInputs& I, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& O, FString& E){ return EvaluateAnyFieldNode(TEXT("Clip"), N, I, O, E, ClipField); });
}

void RegisterEonformDeflateNode()
{
	TArray<FEonformTerrainParameterDescriptor> P;
	P.Add(NumberParam(TEXT("Amount"), TEXT("Amount"), 0.5, 0.0, 1.0));
	RegisterNode(EonformTerrainNodeTypes::Deflate, TEXT("Deflate"), TEXT("Removes low-frequency bulk while preserving finer terrain structure."), MoveTemp(P), [](const FEonformTerrainNode& N, const FEonformTerrainNodeInputs& I, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& O, FString& E){ return EvaluateAnyFieldNode(TEXT("Deflate"), N, I, O, E, DeflateField); });
}

void RegisterEonformDilateNode()
{
	TArray<FEonformTerrainParameterDescriptor> P;
	P.Add(NumberParam(TEXT("Size"), TEXT("Size"), 1.0, 1.0, 16.0));
	P.Add(NameParam(TEXT("Shape"), TEXT("Shape"), TEXT("Rectangle"), { TEXT("Rectangle"), TEXT("Cross"), TEXT("Line"), TEXT("Line Vertical") }));
	P.Add(IntegerParam(TEXT("Iterations"), TEXT("Iterations"), 1, 1, 16));
	P.Add(NameParam(TEXT("Kernel"), TEXT("Kernel"), TEXT("Rectangle"), { TEXT("Cross"), TEXT("Rectangle"), TEXT("Line"), TEXT("Line Vertical") }));
	P.Add(BoolParam(TEXT("Invert"), TEXT("Invert"), false));
	RegisterNode(EonformTerrainNodeTypes::Dilate, TEXT("Dilate"), TEXT("Expands heightfield or mask features using a selectable kernel."), MoveTemp(P), [](const FEonformTerrainNode& N, const FEonformTerrainNodeInputs& I, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& O, FString& E){ return EvaluateAnyFieldNode(TEXT("Dilate"), N, I, O, E, DilateField); });
}

void RegisterEonformEqualizeNode()
{
	TArray<FEonformTerrainParameterDescriptor> P;
	RegisterNode(EonformTerrainNodeTypes::Equalize, TEXT("Equalize"), TEXT("Redistributes values to use the available range more uniformly."), MoveTemp(P), [](const FEonformTerrainNode& N, const FEonformTerrainNodeInputs& I, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& O, FString& E){ return EvaluateAnyFieldNode(TEXT("Equalize"), N, I, O, E, EqualizeField); });
}

void RegisterEonformExtendNode()
{
	TArray<FEonformTerrainParameterDescriptor> P;
	P.Add(NumberParam(TEXT("Value"), TEXT("Value"), 1.0, 0.0, 4.0));
	RegisterNode(EonformTerrainNodeTypes::Extend, TEXT("Extend"), TEXT("Stretches the usable elevation or mask range beyond its current intensity."), MoveTemp(P), [](const FEonformTerrainNode& N, const FEonformTerrainNodeInputs& I, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& O, FString& E){ return EvaluateAnyFieldNode(TEXT("Extend"), N, I, O, E, ExtendField); });
}

void RegisterEonformMedianNode()
{
	TArray<FEonformTerrainParameterDescriptor> P;
	P.Add(IntegerParam(TEXT("Radius"), TEXT("Radius"), 1, 1, 8));
	P.Add(NumberParam(TEXT("Amount"), TEXT("Amount"), 1.0, 0.0, 1.0));
	RegisterNode(EonformTerrainNodeTypes::Median, TEXT("Median"), TEXT("Removes isolated noise while retaining stronger edges using a median filter."), MoveTemp(P), [](const FEonformTerrainNode& N, const FEonformTerrainNodeInputs& I, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& O, FString& E){ return EvaluateAnyFieldNode(TEXT("Median"), N, I, O, E, MedianField); });
}
