#include "EonformModifyProfileNodes.h"

#include "Algo/Sort.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformModifyProfileNodesPrivate
{
	FEonformTerrainPortDescriptor ProfileAnyPort(FName Name, const TCHAR* Label)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		Port.DisplayName = Label;
		return Port;
	}

	FEonformTerrainParameterDescriptor ProfileNumberParam(FName Name, const TCHAR* Label, double DefaultValue, double Min, double Max)
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

	FEonformTerrainParameterDescriptor ProfileIntegerParam(FName Name, const TCHAR* Label, int64 DefaultValue, int64 Min, int64 Max)
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

	FEonformTerrainParameterDescriptor ProfileNameParam(FName Name, const TCHAR* Label, FName DefaultValue, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Name;
		P.DefaultName = DefaultValue;
		for (const FName Option : Options) P.NameOptions.Add(Option);
		return P;
	}

	float ProfileTo01(float V, bool bTerrain)
	{
		return bTerrain ? FMath::Clamp(V * 0.5f + 0.5f, 0.0f, 1.0f) : FMath::Clamp(V, 0.0f, 1.0f);
	}

	float ProfileFrom01(float V, bool bTerrain)
	{
		const float Clamped = FMath::Clamp(V, 0.0f, 1.0f);
		return bTerrain ? Clamped * 2.0f - 1.0f : Clamped;
	}

	float ProfileSampleClamped(const FEonformScalarField& Field, int32 X, int32 Y)
	{
		return Field.AtInterior(
			FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1),
			FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1));
	}

	using FProfileProcessor = TFunction<bool(const FEonformTerrainNode&, const FEonformScalarField&, bool, FEonformScalarField&, FString&)>;

	bool EvaluateProfileNode(const TCHAR* Label, const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, FEonformTerrainNodeEvaluation& Out, FString& Error, const FProfileProcessor& Processor)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid()) { Error = FString::Printf(TEXT("%s requires a valid Input."), Label); return false; }
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
			if (!Height || !Height->IsValid()) { Error = FString::Printf(TEXT("%s terrain input has no valid Height field."), Label); return false; }
			FEonformScalarField ResultHeight;
			if (!Processor(Node, *Height, true, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;
			FEonformTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight))) { Error = FString::Printf(TEXT("%s could not publish its Height field."), Label); return false; }
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
			return true;
		}
		Error = FString::Printf(TEXT("%s received an unsupported input type."), Label);
		return false;
	}

	bool CurveField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool bTerrain, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Curve received an invalid field."); return false; }
		const float Shadows = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shadows"), 0.0)), -1.0f, 1.0f);
		const float Midtones = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Midtones"), 0.0)), -1.0f, 1.0f);
		const float Highlights = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Highlights"), 0.0)), -1.0f, 1.0f);
		const float Contrast = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Contrast"), 0.0)), -1.0f, 1.0f);
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float V = ProfileTo01(Source.AtInterior(X, Y), bTerrain);
				const float ShadowW = FMath::Square(1.0f - V);
				const float HighlightW = FMath::Square(V);
				const float MidW = 4.0f * V * (1.0f - V);
				float R = V + Shadows * ShadowW * 0.35f + Midtones * MidW * 0.25f + Highlights * HighlightW * 0.35f;
				R = (R - 0.5f) * (1.0f + Contrast) + 0.5f;
				OutField.AtInterior(X, Y) = ProfileFrom01(R, bTerrain);
			}
		}
		return true;
	}

	bool FilterField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Filter received an invalid field."); return false; }
		const FName Mode = Node.GetName(TEXT("Mode"), TEXT("LowPass"));
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), 0.0f, 1.0f);
		const int32 Radius = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Radius"), 2)), 1, 16);
		if (Mode != TEXT("LowPass") && Mode != TEXT("HighPass") && Mode != TEXT("BandPass")) { Error = TEXT("Filter Mode must be LowPass, HighPass, or BandPass."); return false; }
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float Sum = 0.0f; int32 Count = 0;
				for (int32 DY = -Radius; DY <= Radius; ++DY)
				{
					for (int32 DX = -Radius; DX <= Radius; ++DX) { Sum += ProfileSampleClamped(Source, X + DX, Y + DY); ++Count; }
				}
				const float Low = Count > 0 ? Sum / static_cast<float>(Count) : Source.AtInterior(X, Y);
				const float Center = Source.AtInterior(X, Y);
				const float High = Center - Low;
				float Target = Low;
				if (Mode == TEXT("HighPass")) Target = FMath::Clamp(High, -1.0f, 1.0f);
				else if (Mode == TEXT("BandPass")) Target = FMath::Clamp(Low + High * 0.5f, -1.0f, 1.0f);
				OutField.AtInterior(X, Y) = FMath::Lerp(Center, Target, Amount);
			}
		}
		return true;
	}

	bool GraphicEQField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool bTerrain, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("GraphicEQ received an invalid field."); return false; }
		const float Low = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Low"), 0.0)), -1.0f, 1.0f);
		const float LowMid = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("LowMid"), 0.0)), -1.0f, 1.0f);
		const float Mid = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Mid"), 0.0)), -1.0f, 1.0f);
		const float HighMid = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HighMid"), 0.0)), -1.0f, 1.0f);
		const float High = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("High"), 0.0)), -1.0f, 1.0f);
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float V = ProfileTo01(Source.AtInterior(X, Y), bTerrain);
				const float B0 = FMath::Clamp(1.0f - FMath::Abs(V - 0.10f) / 0.22f, 0.0f, 1.0f);
				const float B1 = FMath::Clamp(1.0f - FMath::Abs(V - 0.30f) / 0.22f, 0.0f, 1.0f);
				const float B2 = FMath::Clamp(1.0f - FMath::Abs(V - 0.50f) / 0.22f, 0.0f, 1.0f);
				const float B3 = FMath::Clamp(1.0f - FMath::Abs(V - 0.70f) / 0.22f, 0.0f, 1.0f);
				const float B4 = FMath::Clamp(1.0f - FMath::Abs(V - 0.90f) / 0.22f, 0.0f, 1.0f);
				OutField.AtInterior(X, Y) = ProfileFrom01(V + 0.18f * (Low * B0 + LowMid * B1 + Mid * B2 + HighMid * B3 + High * B4), bTerrain);
			}
		}
		return true;
	}

	bool HealField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Heal received an invalid field."); return false; }
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), 0.0f, 1.0f);
		const float Threshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Threshold"), 0.08)), 0.0f, 1.0f);
		const int32 Radius = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Radius"), 1)), 1, 4);
		OutField = Source;
		TArray<float> Samples;
		Samples.Reserve((Radius * 2 + 1) * (Radius * 2 + 1));
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				Samples.Reset();
				for (int32 DY = -Radius; DY <= Radius; ++DY)
				{
					for (int32 DX = -Radius; DX <= Radius; ++DX) Samples.Add(ProfileSampleClamped(Source, X + DX, Y + DY));
				}
				Algo::Sort(Samples);
				const float Median = Samples[Samples.Num() / 2];
				const float Center = Source.AtInterior(X, Y);
				OutField.AtInterior(X, Y) = FMath::Lerp(Center, FMath::Abs(Center - Median) >= Threshold ? Median : Center, Amount);
			}
		}
		return true;
	}

	bool MatchField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool bTerrain, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Match received an invalid field."); return false; }
		const float TargetMean = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("TargetMean"), 0.5)), 0.0f, 1.0f);
		const float TargetContrast = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("TargetContrast"), 0.5)), 0.0f, 1.0f);
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 1.0)), 0.0f, 1.0f);
		const int32 Count = Source.Domain.Dimensions.X * Source.Domain.Dimensions.Y;
		if (Count <= 0) return false;
		double Sum = 0.0, SumSq = 0.0;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float V = ProfileTo01(Source.AtInterior(X, Y), bTerrain); Sum += V; SumSq += static_cast<double>(V) * V;
			}
		}
		const float Mean = static_cast<float>(Sum / Count);
		const float Std = FMath::Sqrt(FMath::Max(0.0f, static_cast<float>(SumSq / Count) - Mean * Mean));
		const float TargetStd = FMath::Lerp(0.02f, 0.35f, TargetContrast);
		const float Scale = TargetStd / FMath::Max(Std, 0.0001f);
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float V = ProfileTo01(Source.AtInterior(X, Y), bTerrain);
				OutField.AtInterior(X, Y) = ProfileFrom01(FMath::Lerp(V, FMath::Clamp((V - Mean) * Scale + TargetMean, 0.0f, 1.0f), Amount), bTerrain);
			}
		}
		return true;
	}

	bool PixelateField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Pixelate received an invalid field."); return false; }
		const int32 Size = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Size"), 4)), 1, 128);
		const FName Sampling = Node.GetName(TEXT("Sampling"), TEXT("Average"));
		if (Sampling != TEXT("Average") && Sampling != TEXT("Center")) { Error = TEXT("Pixelate Sampling must be Average or Center."); return false; }
		OutField = Source;
		for (int32 BY = 0; BY < Source.Domain.Dimensions.Y; BY += Size)
		{
			for (int32 BX = 0; BX < Source.Domain.Dimensions.X; BX += Size)
			{
				float Value = 0.0f;
				if (Sampling == TEXT("Center")) Value = ProfileSampleClamped(Source, BX + Size / 2, BY + Size / 2);
				else
				{
					double Sum = 0.0; int32 Count = 0;
					for (int32 Y = BY; Y < FMath::Min(BY + Size, Source.Domain.Dimensions.Y); ++Y)
					{
						for (int32 X = BX; X < FMath::Min(BX + Size, Source.Domain.Dimensions.X); ++X) { Sum += Source.AtInterior(X, Y); ++Count; }
					}
					Value = Count > 0 ? static_cast<float>(Sum / Count) : 0.0f;
				}
				for (int32 Y = BY; Y < FMath::Min(BY + Size, Source.Domain.Dimensions.Y); ++Y)
				{
					for (int32 X = BX; X < FMath::Min(BX + Size, Source.Domain.Dimensions.X); ++X) OutField.AtInterior(X, Y) = Value;
				}
			}
		}
		return true;
	}

	void RegisterProfileSimple(FName Type, const TCHAR* DisplayName, const TCHAR* Description, std::initializer_list<FEonformTerrainParameterDescriptor> Parameters, const FProfileProcessor& Processor)
	{
		FEonformTerrainNodeDescriptor Descriptor;
		Descriptor.Type = Type;
		Descriptor.DisplayName = DisplayName;
		Descriptor.Category = TEXT("Modify");
		Descriptor.Description = Description;
		Descriptor.Inputs.Add(ProfileAnyPort(TEXT("Input"), TEXT("Input")));
		Descriptor.Outputs.Add(ProfileAnyPort(TEXT("Out"), TEXT("Out")));
		for (const FEonformTerrainParameterDescriptor& P : Parameters) Descriptor.Parameters.Add(P);
		FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
		const FString Label(DisplayName);
		FEonformTerrainNodeRegistry::Register(Type, [Label, Processor](const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
		{
			return EvaluateProfileNode(*Label, Node, Inputs, Out, Error, Processor);
		});
	}
}

void RegisterEonformCurveNode()
{
	using namespace EonformModifyProfileNodesPrivate;
	RegisterProfileSimple(EonformTerrainNodeTypes::Curve, TEXT("Curve"), TEXT("Remaps terrain or mask values with shadow, midtone, highlight, and contrast controls."),
		{ ProfileNumberParam(TEXT("Shadows"), TEXT("Shadows"), 0.0, -1.0, 1.0), ProfileNumberParam(TEXT("Midtones"), TEXT("Midtones"), 0.0, -1.0, 1.0), ProfileNumberParam(TEXT("Highlights"), TEXT("Highlights"), 0.0, -1.0, 1.0), ProfileNumberParam(TEXT("Contrast"), TEXT("Contrast"), 0.0, -1.0, 1.0) }, CurveField);
}
void RegisterEonformFilterNode()
{
	using namespace EonformModifyProfileNodesPrivate;
	RegisterProfileSimple(EonformTerrainNodeTypes::Filter, TEXT("Filter"), TEXT("Applies low-pass, high-pass, or band-pass filtering to terrain or masks."),
		{ ProfileNameParam(TEXT("Mode"), TEXT("Mode"), TEXT("LowPass"), { TEXT("LowPass"), TEXT("HighPass"), TEXT("BandPass") }), ProfileNumberParam(TEXT("Amount"), TEXT("Amount"), 0.5, 0.0, 1.0), ProfileIntegerParam(TEXT("Radius"), TEXT("Radius"), 2, 1, 16) }, FilterField);
}
void RegisterEonformGraphicEQNode()
{
	using namespace EonformModifyProfileNodesPrivate;
	RegisterProfileSimple(EonformTerrainNodeTypes::GraphicEQ, TEXT("GraphicEQ"), TEXT("Shapes low, mid, and high elevation bands independently."),
		{ ProfileNumberParam(TEXT("Low"), TEXT("Low"), 0.0, -1.0, 1.0), ProfileNumberParam(TEXT("LowMid"), TEXT("Low Mid"), 0.0, -1.0, 1.0), ProfileNumberParam(TEXT("Mid"), TEXT("Mid"), 0.0, -1.0, 1.0), ProfileNumberParam(TEXT("HighMid"), TEXT("High Mid"), 0.0, -1.0, 1.0), ProfileNumberParam(TEXT("High"), TEXT("High"), 0.0, -1.0, 1.0) }, GraphicEQField);
}
void RegisterEonformHealNode()
{
	using namespace EonformModifyProfileNodesPrivate;
	RegisterProfileSimple(EonformTerrainNodeTypes::Heal, TEXT("Heal"), TEXT("Repairs isolated spikes and pits while preserving surrounding terrain."),
		{ ProfileNumberParam(TEXT("Amount"), TEXT("Amount"), 0.5, 0.0, 1.0), ProfileNumberParam(TEXT("Threshold"), TEXT("Threshold"), 0.08, 0.0, 1.0), ProfileIntegerParam(TEXT("Radius"), TEXT("Radius"), 1, 1, 4) }, HealField);
}
void RegisterEonformMatchNode()
{
	using namespace EonformModifyProfileNodesPrivate;
	RegisterProfileSimple(EonformTerrainNodeTypes::Match, TEXT("Match"), TEXT("Matches mean elevation and contrast to a target statistical profile."),
		{ ProfileNumberParam(TEXT("TargetMean"), TEXT("Target Mean"), 0.5, 0.0, 1.0), ProfileNumberParam(TEXT("TargetContrast"), TEXT("Target Contrast"), 0.5, 0.0, 1.0), ProfileNumberParam(TEXT("Amount"), TEXT("Amount"), 1.0, 0.0, 1.0) }, MatchField);
}
void RegisterEonformPixelateNode()
{
	using namespace EonformModifyProfileNodesPrivate;
	RegisterProfileSimple(EonformTerrainNodeTypes::Pixelate, TEXT("Pixelate"), TEXT("Reduces spatial detail into configurable sample blocks."),
		{ ProfileIntegerParam(TEXT("Size"), TEXT("Size"), 4, 1, 128), ProfileNameParam(TEXT("Sampling"), TEXT("Sampling"), TEXT("Average"), { TEXT("Average"), TEXT("Center") }) }, PixelateField);
}
