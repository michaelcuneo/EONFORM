#include "EonformCombineNode.h"

#include "EonformTerrainDomainScaling.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor AnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor NameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.Type = EEonformTerrainParameterType::Name;
		P.DefaultName = DefaultValue;
		for (const FName Option : Options) P.NameOptions.Add(Option);
		return P;
	}

	FEonformTerrainParameterDescriptor NumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.Type = EEonformTerrainParameterType::Number;
		P.DefaultNumber = DefaultValue;
		P.bHasMinimum = true;
		P.Minimum = Minimum;
		P.bHasMaximum = true;
		P.Maximum = Maximum;
		return P;
	}

	FEonformTerrainParameterDescriptor BoolParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.Type = EEonformTerrainParameterType::Boolean;
		P.DefaultBoolean = DefaultValue;
		return P;
	}

	const FEonformTerrainValue* Input(const FEonformTerrainNodeInputs& Inputs, FName Name)
	{
		const FEonformTerrainValue* const* P = Inputs.Find(Name);
		return P ? *P : nullptr;
	}

	const FEonformScalarField* AsScalar(const FEonformTerrainValue* Value)
	{
		if (!Value || !Value->IsValid()) return nullptr;
		if (Value->Type == EEonformTerrainValueType::ScalarField) return &Value->ScalarField;
		if (Value->Type == EEonformTerrainValueType::Terrain) return Value->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		return nullptr;
	}

	FName CanonicalMode(FName Mode)
	{
		if (Mode == TEXT("Divide 2")) return TEXT("Divide2");
		if (Mode == TEXT("Soft Light")) return TEXT("SoftLight");
		if (Mode == TEXT("Hard Light")) return TEXT("HardLight");
		if (Mode == TEXT("Pin Light")) return TEXT("PinLight");
		if (Mode == TEXT("Grain Merge")) return TEXT("GrainMerge");
		if (Mode == TEXT("Grain Extract")) return TEXT("GrainExtract");
		return Mode;
	}

	float To01(float V) { return FMath::Clamp(V * 0.5f + 0.5f, 0.0f, 1.0f); }
	float From01(float V) { return FMath::Clamp(V, 0.0f, 1.0f) * 2.0f - 1.0f; }
	float From01Unclamped(float V) { return V * 2.0f - 1.0f; }

	void Autolevel(TArray<float>& Values)
	{
		if (Values.IsEmpty()) return;
		float Min = TNumericLimits<float>::Max();
		float Max = TNumericLimits<float>::Lowest();
		for (const float V : Values) { Min = FMath::Min(Min, V); Max = FMath::Max(Max, V); }
		const float Range = Max - Min;
		if (Range <= UE_SMALL_NUMBER) return;
		for (float& V : Values) V = (V - Min) / Range;
	}

	void Equalize(TArray<float>& Values)
	{
		if (Values.IsEmpty()) return;
		constexpr int32 BinCount = 1024;
		TArray<int32> Histogram;
		Histogram.SetNumZeroed(BinCount);
		for (const float V : Values)
		{
			const int32 Bin = FMath::Clamp(FMath::FloorToInt(FMath::Clamp(V, 0.0f, 1.0f) * (BinCount - 1)), 0, BinCount - 1);
			++Histogram[Bin];
		}
		TArray<float> Cdf;
		Cdf.SetNumZeroed(BinCount);
		int32 Running = 0;
		for (int32 I = 0; I < BinCount; ++I)
		{
			Running += Histogram[I];
			Cdf[I] = static_cast<float>(Running) / Values.Num();
		}
		for (float& V : Values)
		{
			const int32 Bin = FMath::Clamp(FMath::FloorToInt(FMath::Clamp(V, 0.0f, 1.0f) * (BinCount - 1)), 0, BinCount - 1);
			V = Cdf[Bin];
		}
	}

	bool MakeCompatible(
		const FEonformScalarField& Source,
		const FEonformGridDomain& Domain,
		FEonformScalarField& Storage,
		const FEonformScalarField*& Out,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Combine received an invalid scalar field.");
			return false;
		}
		if (Source.Domain == Domain)
		{
			Out = &Source;
			return true;
		}
		if (!EonformTerrainDomainScaling::ResampleScalarField(Source, Domain, Storage, &Error)) return false;
		Out = &Storage;
		return true;
	}

	bool EvaluateCombineNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext&,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* AValue = Input(Inputs, TEXT("Input1"));
		const FEonformTerrainValue* BValue = Input(Inputs, TEXT("Input2"));
		if (!AValue || !BValue || !AValue->IsValid() || !BValue->IsValid() || AValue->Type != BValue->Type)
		{
			Error = TEXT("Combine requires valid Input1 and Input2 values of the same type.");
			return false;
		}

		if (Node.GetBool(TEXT("SwapInputs"), false)) Swap(AValue, BValue);
		const FEonformScalarField* A = AsScalar(AValue);
		const FEonformScalarField* BSource = AsScalar(BValue);
		if (!A || !BSource)
		{
			Error = TEXT("Combine inputs must provide scalar fields.");
			return false;
		}

		FEonformScalarField BStorage;
		const FEonformScalarField* B = nullptr;
		if (!MakeCompatible(*BSource, A->Domain, BStorage, B, Error)) return false;

		const FEonformScalarField* MaskSource = AsScalar(Input(Inputs, TEXT("Mask")));
		FEonformScalarField MaskStorage;
		const FEonformScalarField* Mask = nullptr;
		if (MaskSource && !MakeCompatible(*MaskSource, A->Domain, MaskStorage, Mask, Error)) return false;

		const bool bTerrain = AValue->Type == EEonformTerrainValueType::Terrain;
		const float Ratio = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Ratio"), 0.5)), 0.0f, 1.0f);
		const FName Mode = CanonicalMode(Node.GetName(TEXT("Mode"), TEXT("Blend")));
		const FName OutputMode = Node.GetName(TEXT("Output"), TEXT("Clamp"));
		const FName Enhance = Node.GetName(TEXT("Enhance"), TEXT("None"));
		const bool bRawTerrainMultiply = bTerrain && Mode == TEXT("Multiply") && Enhance == TEXT("None");

		FEonformScalarField Result = *A;
		TArray<float> A01;
		TArray<float> B01;
		A01.SetNumUninitialized(Result.Values.Num());
		B01.SetNumUninitialized(Result.Values.Num());
		for (int32 Y = 0; Y < A->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < A->Domain.Dimensions.X; ++X)
			{
				const int32 I = Result.Domain.GetStorageIndex(X, Y);
				A01[I] = bTerrain ? To01(A->AtInterior(X, Y)) : FMath::Clamp(A->AtInterior(X, Y), 0.0f, 1.0f);
				B01[I] = bTerrain ? To01(B->AtInterior(X, Y)) : FMath::Clamp(B->AtInterior(X, Y), 0.0f, 1.0f);
			}
		}

		if (Enhance == TEXT("Autolevel"))
		{
			Autolevel(A01);
			Autolevel(B01);
		}
		else if (Enhance == TEXT("Equalize"))
		{
			Equalize(A01);
			Equalize(B01);
		}

		for (int32 Y = 0; Y < A->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < A->Domain.Dimensions.X; ++X)
			{
				const int32 I = Result.Domain.GetStorageIndex(X, Y);
				const float ARaw = A->AtInterior(X, Y);
				const float BRaw = B->AtInterior(X, Y);
				float V;
				if (bRawTerrainMultiply)
				{
					const float Raw = FMath::Lerp(ARaw, ARaw * BRaw, Ratio);
					V = Raw * 0.5f + 0.5f;
				}
				else
				{
					V = FMath::Lerp(A01[I], EonformCombine::ApplyMode(A01[I], B01[I], Mode), Ratio);
				}

				if (Mask)
				{
					const float M = FMath::Clamp(Mask->AtInterior(X, Y), 0.0f, 1.0f);
					V = FMath::Lerp(B01[I], A01[I], M);
				}

				if (OutputMode == TEXT("Clamp")) V = FMath::Clamp(V, 0.0f, 1.0f);
				else if (OutputMode == TEXT("Extend")) V = 0.5f + 0.5f * FMath::Tanh((V - 0.5f) * 2.0f);
				Result.AtInterior(X, Y) = bTerrain
					? (OutputMode == TEXT("None") ? From01Unclamped(V) : From01(V))
					: (OutputMode == TEXT("Clamp") ? FMath::Clamp(V, 0.0f, 1.0f) : V);
			}
		}

		if (bTerrain)
		{
			Result.Descriptor.Name = EonformTerrainFieldNames::Height;
			FEonformTerrainDataset Dataset = AValue->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(Result)))
			{
				Error = TEXT("Combine could not publish Height.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), AValue->HeightScale));
			return true;
		}

		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Result)));
		return true;
	}
}

float EonformCombine::ApplyMode(float A, float B, FName Mode)
{
	Mode = CanonicalMode(Mode);
	if (Mode == TEXT("Blend")) return B;
	if (Mode == TEXT("Add")) return A + B;
	if (Mode == TEXT("Screen")) return 1.0f - (1.0f - A) * (1.0f - B);
	if (Mode == TEXT("Subtract")) return A - B;
	if (Mode == TEXT("Difference")) return FMath::Abs(A - B);
	if (Mode == TEXT("Multiply")) return A * B;
	if (Mode == TEXT("Divide")) return FMath::Abs(B) > UE_SMALL_NUMBER ? A / B : A;
	if (Mode == TEXT("Divide2")) return FMath::Abs(A) > UE_SMALL_NUMBER ? B / A : B;
	if (Mode == TEXT("Max")) return FMath::Max(A, B);
	if (Mode == TEXT("Min")) return FMath::Min(A, B);
	if (Mode == TEXT("Hypotenuse")) return FMath::Sqrt(A * A + B * B);
	if (Mode == TEXT("Overlay")) return A < 0.5f ? 2.0f * A * B : 1.0f - 2.0f * (1.0f - A) * (1.0f - B);
	if (Mode == TEXT("Power")) return FMath::Pow(FMath::Max(A, UE_SMALL_NUMBER), B);
	if (Mode == TEXT("Exclusion")) return A + B - 2.0f * A * B;
	if (Mode == TEXT("Dodge")) return B >= 1.0f ? 1.0f : A / FMath::Max(1.0f - B, UE_SMALL_NUMBER);
	if (Mode == TEXT("Burn")) return B <= 0.0f ? 0.0f : 1.0f - (1.0f - A) / FMath::Max(B, UE_SMALL_NUMBER);
	if (Mode == TEXT("SoftLight")) return (1.0f - 2.0f * B) * A * A + 2.0f * B * A;
	if (Mode == TEXT("HardLight")) return B < 0.5f ? 2.0f * A * B : 1.0f - 2.0f * (1.0f - A) * (1.0f - B);
	if (Mode == TEXT("PinLight")) return B < 0.5f ? FMath::Min(A, 2.0f * B) : FMath::Max(A, 2.0f * B - 1.0f);
	if (Mode == TEXT("GrainMerge")) return A + B - 0.5f;
	if (Mode == TEXT("GrainExtract")) return A - B + 0.5f;
	if (Mode == TEXT("Reflect")) return B >= 1.0f ? 1.0f : A * A / FMath::Max(1.0f - B, UE_SMALL_NUMBER);
	if (Mode == TEXT("Glow")) return A >= 1.0f ? 1.0f : B * B / FMath::Max(1.0f - A, UE_SMALL_NUMBER);
	if (Mode == TEXT("Phoenix")) return A - B + FMath::Max(A, B);
	return B;
}

bool EonformCombine::ApplyRawFields(
	const FEonformScalarField& A,
	const FEonformScalarField& B,
	FName Mode,
	float Ratio,
	FEonformScalarField& OutField,
	FString* OutError)
{
	if (!A.IsValid() || !B.IsValid() || A.Domain != B.Domain)
	{
		if (OutError) *OutError = TEXT("Combine raw fields require matching valid domains.");
		return false;
	}
	const float BlendRatio = FMath::Clamp(Ratio, 0.0f, 1.0f);
	OutField = A;
	for (int32 I = 0; I < OutField.Values.Num(); ++I)
	{
		OutField.Values[I] = FMath::Lerp(A.Values[I], ApplyMode(A.Values[I], B.Values[I], Mode), BlendRatio);
	}
	if (OutError) OutError->Reset();
	return OutField.IsValid();
}

void RegisterEonformCombineNode()
{
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Combine;
	D.DisplayName = TEXT("Combine");
	D.Category = TEXT("Utility");
	D.Description = TEXT("Combines two data sources using blend modes, input enhancement, output range control, swap and mask routing.");
	D.Inputs.Add(AnyPort(TEXT("Input1"), TEXT("Input1")));
	D.Inputs.Add(AnyPort(TEXT("Input2"), TEXT("Input2")));
	D.Inputs.Add(ScalarPort(TEXT("Mask"), TEXT("Mask")));
	D.Outputs.Add(AnyPort(TEXT("Out"), TEXT("Out")));
	D.Parameters.Add(NumberParameter(TEXT("Ratio"), TEXT("Ratio"), 0.5, 0.0, 1.0));
	D.Parameters.Add(NameParameter(TEXT("Mode"), TEXT("Mode"), TEXT("Blend"), { TEXT("Blend"), TEXT("Add"), TEXT("Screen"), TEXT("Subtract"), TEXT("Difference"), TEXT("Multiply"), TEXT("Divide"), TEXT("Divide2"), TEXT("Max"), TEXT("Min"), TEXT("Hypotenuse"), TEXT("Overlay"), TEXT("Power"), TEXT("Exclusion"), TEXT("Dodge"), TEXT("Burn"), TEXT("SoftLight"), TEXT("HardLight"), TEXT("PinLight"), TEXT("GrainMerge"), TEXT("GrainExtract"), TEXT("Reflect"), TEXT("Glow"), TEXT("Phoenix") }));
	D.Parameters.Add(NameParameter(TEXT("Output"), TEXT("Output"), TEXT("Clamp"), { TEXT("None"), TEXT("Clamp"), TEXT("Extend") }));
	D.Parameters.Add(NameParameter(TEXT("Enhance"), TEXT("Enhance Input"), TEXT("None"), { TEXT("None"), TEXT("Autolevel"), TEXT("Equalize") }));
	D.Parameters.Add(BoolParameter(TEXT("SwapInputs"), TEXT("Swap Inputs"), false));
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(D.Type, EvaluateCombineNode);
}
