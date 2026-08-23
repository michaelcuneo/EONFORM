#include "GaeaReferenceFidelityNodes.h"

#include "GaeaColorField.h"
#include "GaeaGridDomain.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainPhysicalMetrics.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaReferenceFidelity
{
	FGaeaTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = Type;
		return P;
	}

	FGaeaTerrainParameterDescriptor Number(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Integer(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = static_cast<double>(Max);
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Boolean(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName Option : Options) P.NameOptions.Add(Option);
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaGridDomain BuildSourceDomain(const FGaeaTerrainNode& Node, const FGaeaTerrainEvaluationContext& Context)
	{
		const int32 RequestedX = Context.TargetResolution.X > 1 ? Context.TargetResolution.X : 257;
		const int32 RequestedY = Context.TargetResolution.Y > 1 ? Context.TargetResolution.Y : RequestedX;
		const int32 LegacyResolution = static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 0));
		const int32 Width = FMath::Clamp(LegacyResolution > 1 ? LegacyResolution : RequestedX, 2, 4097);
		const int32 Height = FMath::Clamp(LegacyResolution > 1 ? LegacyResolution : RequestedY, 2, 4097);

		double WorldWidthCm = 100000.0;
		double WorldDepthCm = 100000.0;
		if (Context.PhysicalMetrics.HasWorldDimensions())
		{
			WorldWidthCm = Context.PhysicalMetrics.WorldWidthMeters * 100.0;
			WorldDepthCm = Context.PhysicalMetrics.WorldDepthMeters * 100.0;
		}
		else
		{
			const double LegacyWorld = FMath::Max(Node.GetNumber(TEXT("WorldSize"), 100000.0), 1.0);
			WorldWidthCm = LegacyWorld;
			WorldDepthCm = LegacyWorld;
		}

		return FGaeaGridDomain::Make(
			FIntPoint(Width, Height),
			FVector2d(-WorldWidthCm * 0.5, -WorldDepthCm * 0.5),
			FVector2d(WorldWidthCm * 0.5, WorldDepthCm * 0.5));
	}

	float ResolveHeightScale(const FGaeaTerrainNode& Node, const FGaeaTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale())
		{
			return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		}
		return FMath::Max(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), Context.HeightScale)), 1.0f);
	}

	FGaeaScalarField MakeHeight(const FGaeaGridDomain& Domain)
	{
		FGaeaFieldDescriptor D;
		D.Name = GaeaTerrainFieldNames::Height;
		D.Unit = EGaeaFieldUnit::Normalized;
		D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, D, 0.0f);
		return Field;
	}

	bool PublishHeight(FGaeaScalarField&& Height, float HeightScale, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Reference-fidelity node could not publish Height.");
			return false;
		}
		FGaeaTerrainValue Value = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Value.IsValid())
		{
			Error = TEXT("Reference-fidelity node produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Value));
		return true;
	}

	uint32 Hash(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352dU;
		X ^= X >> 15;
		X *= 0x846ca68bU;
		X ^= X >> 16;
		return X;
	}

	float Hash01(int32 X, int32 Y, int32 Seed, uint32 Salt = 0)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U;
		H ^= static_cast<uint32>(Y) * 0x85ebca6bU;
		H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U;
		H ^= Salt;
		return static_cast<float>(Hash(H) & 0x00ffffffU) / 16777215.0f;
	}

	float SmoothNoise(float X, float Y, int32 Seed, uint32 Salt)
	{
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const float TX0 = X - static_cast<float>(X0);
		const float TY0 = Y - static_cast<float>(Y0);
		const float TX = TX0 * TX0 * (3.0f - 2.0f * TX0);
		const float TY = TY0 * TY0 * (3.0f - 2.0f * TY0);
		const float A = FMath::Lerp(Hash01(X0, Y0, Seed, Salt), Hash01(X0 + 1, Y0, Seed, Salt), TX);
		const float B = FMath::Lerp(Hash01(X0, Y0 + 1, Seed, Salt), Hash01(X0 + 1, Y0 + 1, Seed, Salt), TX);
		return FMath::Lerp(A, B, TY) * 2.0f - 1.0f;
	}

	float Fbm(float X, float Y, float Frequency, int32 Octaves, float Roughness, int32 Seed, uint32 Salt)
	{
		float Sum = 0.0f;
		float Amp = 1.0f;
		float Weight = 0.0f;
		for (int32 I = 0; I < Octaves; ++I)
		{
			Sum += SmoothNoise(X * Frequency, Y * Frequency, Seed + I * 193, Salt + I * 7919u) * Amp;
			Weight += Amp;
			Frequency *= 2.03f;
			Amp *= Roughness;
		}
		return Weight > UE_SMALL_NUMBER ? Sum / Weight : 0.0f;
	}

	bool EvaluateRadialGradient(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaGridDomain Domain = BuildSourceDomain(Node, Context);
		if (!Domain.IsValid()) { Error = TEXT("RadialGradient produced an invalid domain."); return false; }

		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.001f, 4.0f);
		const float PeakHeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), -4.0f, 4.0f);
		const float OffsetX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("X"), 0.0)), -2.0f, 2.0f);
		const float OffsetY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0)), -2.0f, 2.0f);

		FGaeaScalarField Height = MakeHeight(Domain);
		const FVector2d WorldSize = Domain.WorldSize();
		const double Reference = FMath::Max(FMath::Min(FMath::Abs(WorldSize.X), FMath::Abs(WorldSize.Y)), UE_DOUBLE_SMALL_NUMBER);
		const FVector2d Center(
			Domain.WorldMin.X + WorldSize.X * (0.5 + OffsetX * 0.5),
			Domain.WorldMin.Y + WorldSize.Y * (0.5 + OffsetY * 0.5));
		const double Radius = FMath::Max(Reference * 0.5 * Scale, UE_DOUBLE_SMALL_NUMBER);
		for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Domain.Dimensions.X; ++X)
			{
				const FVector2d W = Domain.InteriorSampleToWorld(X, Y);
				const double D = FVector2d::Distance(W, Center) / Radius;
				const float T = FMath::Clamp(1.0f - static_cast<float>(D), 0.0f, 1.0f);
				Height.AtInterior(X, Y) = T * PeakHeight;
			}
		}
		return PublishHeight(MoveTemp(Height), ResolveHeightScale(Node, Context), Out, Error);
	}

	struct FVoronoiSample
	{
		float F1 = 9999.0f;
		float F2 = 9999.0f;
		float Cell = 0.0f;
	};

	FVoronoiSample SampleVoronoi(float X, float Y, float Jitter, FName Function, int32 Seed)
	{
		const int32 BX = FMath::FloorToInt(X);
		const int32 BY = FMath::FloorToInt(Y);
		FVoronoiSample S;
		for (int32 CY = BY - 2; CY <= BY + 2; ++CY)
		{
			for (int32 CX = BX - 2; CX <= BX + 2; ++CX)
			{
				const float FX = static_cast<float>(CX) + 0.5f + (Hash01(CX, CY, Seed, 0x17u) - 0.5f) * Jitter;
				const float FY = static_cast<float>(CY) + 0.5f + (Hash01(CY, CX, Seed, 0x91u) - 0.5f) * Jitter;
				const float DX = X - FX;
				const float DY = Y - FY;
				const float D = Function == TEXT("Manhattan") ? FMath::Abs(DX) + FMath::Abs(DY) : FMath::Sqrt(DX * DX + DY * DY);
				if (D < S.F1)
				{
					S.F2 = S.F1;
					S.F1 = D;
					S.Cell = Hash01(CX, CY, Seed, 0x71u);
				}
				else if (D < S.F2)
				{
					S.F2 = D;
				}
			}
		}
		return S;
	}

	float VoronoiFormValue(const FVoronoiSample& S, FName Form)
	{
		const float F1 = FMath::Clamp(S.F1, 0.0f, 1.5f);
		const float F2 = FMath::Clamp(S.F2, 0.0f, 2.0f);
		const float EdgeDistance = FMath::Max(F2 - F1, 0.0f);
		const float Peak = FMath::Clamp(1.0f - F1, 0.0f, 1.0f);
		const float Neighbor = FMath::Clamp(1.0f - F2 * 0.72f, 0.0f, 1.0f);
		const float Ridge = FMath::Clamp(1.0f - EdgeDistance * 2.15f, 0.0f, 1.0f);

		if (Form == TEXT("C")) return S.Cell;
		if (Form == TEXT("N")) return Neighbor;
		if (Form == TEXT("R")) return FMath::Pow(Ridge, 1.45f);
		if (Form == TEXT("S")) return FMath::Pow(Peak, 2.45f) * FMath::Pow(FMath::Clamp(EdgeDistance * 2.0f, 0.0f, 1.0f), 0.42f);
		if (Form == TEXT("M")) return FMath::Clamp(Peak * (0.62f + S.Cell * 0.68f), 0.0f, 1.0f);
		if (Form == TEXT("D")) return FMath::Pow(Ridge, 2.15f);
		if (Form == TEXT("A"))
		{
			const float M = FMath::Clamp(Peak * (0.62f + S.Cell * 0.68f), 0.0f, 1.0f);
			const float P = FMath::Pow(Peak, 1.38f) * FMath::Lerp(0.76f, 1.18f, S.Cell);
			return FMath::Clamp(FMath::Lerp(P, M, 0.5f), 0.0f, 1.0f);
		}

		// P: terrain-oriented peaked cells with stronger cell-to-cell elevation
		// variation and shoulders that remain useful for later erosion.
		return FMath::Clamp(FMath::Pow(Peak, 1.34f) * FMath::Lerp(0.72f, 1.20f, S.Cell), 0.0f, 1.0f);
	}

	bool EvaluateVoronoi(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaGridDomain Domain = BuildSourceDomain(Node, Context);
		if (!Domain.IsValid()) { Error = TEXT("Voronoi produced an invalid domain."); return false; }

		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.001f, 16.0f);
		const float Jitter = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Jitter"), 1.0)), 0.0f, 2.0f);
		const FName Function = Node.GetName(TEXT("Function"), TEXT("Euclidean"));
		const FName Form = Node.GetName(TEXT("Form"), TEXT("P"));
		const float Gain = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Gain"), 1.0)), 0.0f, 4.0f);
		const float ClampV = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Clamp"), 1.0)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const FName WarpType = Node.GetName(TEXT("WarpType"), TEXT("None"));
		const float WarpFrequency = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpFrequency"), 1.0)), 0.001f, 16.0f);
		const float WarpAmplitude = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpAmplitude"), 0.0)), 0.0f, 4.0f);
		const int32 WarpOctaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("WarpOctaves"), 1)), 1, 16);
		const float ScaleX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), 1.0)), 0.001f, 16.0f);
		const float ScaleY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), 1.0)), 0.001f, 16.0f);
		const float OffsetX = static_cast<float>(Node.GetNumber(TEXT("X"), 0.0));
		const float OffsetY = static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0));

		FGaeaScalarField Height = MakeHeight(Domain);
		const int32 W = Domain.Dimensions.X;
		const int32 H = Domain.Dimensions.Y;
		for (int32 Y = 0; Y < H; ++Y)
		{
			const float V = H > 1 ? static_cast<float>(Y) / static_cast<float>(H - 1) - 0.5f : 0.0f;
			for (int32 X = 0; X < W; ++X)
			{
				const float U = W > 1 ? static_cast<float>(X) / static_cast<float>(W - 1) - 0.5f : 0.0f;
				float PX = (U + OffsetX) * 18.0f * Scale * ScaleX;
				float PY = (V + OffsetY) * 18.0f * Scale * ScaleY;
				if (WarpType != TEXT("None") && WarpAmplitude > UE_SMALL_NUMBER)
				{
					const int32 Octaves = WarpType == TEXT("Complex") ? FMath::Max(WarpOctaves, 3) : WarpOctaves;
					const float Roughness = WarpType == TEXT("Complex") ? 0.58f : 0.48f;
					const float WX = Fbm(U, V, WarpFrequency * 3.5f, Octaves, Roughness, Seed + 1201, 0x31u);
					const float WY = Fbm(U, V, WarpFrequency * 3.5f, Octaves, Roughness, Seed + 2707, 0x57u);
					PX += WX * WarpAmplitude * 2.2f;
					PY += WY * WarpAmplitude * 2.2f;
				}
				float Value = VoronoiFormValue(SampleVoronoi(PX, PY, Jitter, Function, Seed), Form);
				if (!FMath::IsNearlyEqual(Gain, 1.0f)) Value = FMath::Pow(FMath::Clamp(Value, 0.0f, 1.0f), 1.0f / FMath::Max(Gain, 0.001f));
				Height.AtInterior(X, Y) = FMath::Min(Value, ClampV);
			}
		}
		return PublishHeight(MoveTemp(Height), ResolveHeightScale(Node, Context), Out, Error);
	}

	const FGaeaTerrainValue* Input(const FGaeaTerrainNodeInputs& Inputs, FName Name)
	{
		const FGaeaTerrainValue* const* P = Inputs.Find(Name);
		return P ? *P : nullptr;
	}

	const FGaeaScalarField* AsScalar(const FGaeaTerrainValue* V)
	{
		if (!V || !V->IsValid()) return nullptr;
		if (V->Type == EGaeaTerrainValueType::ScalarField) return &V->ScalarField;
		if (V->Type == EGaeaTerrainValueType::Terrain) return V->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		return nullptr;
	}

	float To01(float V) { return FMath::Clamp(V * 0.5f + 0.5f, 0.0f, 1.0f); }
	float From01(float V) { return FMath::Clamp(V, 0.0f, 1.0f) * 2.0f - 1.0f; }
	float From01Unclamped(float V) { return V * 2.0f - 1.0f; }

	FName CanonicalCombineMode(FName Mode)
	{
		if (Mode == TEXT("Divide 2")) return TEXT("Divide2");
		if (Mode == TEXT("Soft Light")) return TEXT("SoftLight");
		if (Mode == TEXT("Hard Light")) return TEXT("HardLight");
		if (Mode == TEXT("Pin Light")) return TEXT("PinLight");
		if (Mode == TEXT("Grain Merge")) return TEXT("GrainMerge");
		if (Mode == TEXT("Grain Extract")) return TEXT("GrainExtract");
		return Mode;
	}

	float BlendMode(float A, float B, FName Mode)
	{
		Mode = CanonicalCombineMode(Mode);
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
		TArray<int32> Hist;
		Hist.SetNumZeroed(BinCount);
		for (const float V : Values) ++Hist[FMath::Clamp(FMath::FloorToInt(FMath::Clamp(V, 0.0f, 1.0f) * (BinCount - 1)), 0, BinCount - 1)];
		TArray<float> Cdf;
		Cdf.SetNumZeroed(BinCount);
		int32 Running = 0;
		for (int32 I = 0; I < BinCount; ++I) { Running += Hist[I]; Cdf[I] = static_cast<float>(Running) / Values.Num(); }
		for (float& V : Values)
		{
			const int32 Bin = FMath::Clamp(FMath::FloorToInt(FMath::Clamp(V, 0.0f, 1.0f) * (BinCount - 1)), 0, BinCount - 1);
			V = Cdf[Bin];
		}
	}

	bool EvaluateCombine(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* AValue = Input(Inputs, TEXT("Input1"));
		const FGaeaTerrainValue* BValue = Input(Inputs, TEXT("Input2"));
		if (!AValue || !BValue || !AValue->IsValid() || !BValue->IsValid())
		{
			Error = TEXT("Combine requires valid Input1 and Input2 values.");
			return false;
		}

		const bool bSwapInputs = Node.GetBool(TEXT("SwapInputs"), false);
		if (bSwapInputs) Swap(AValue, BValue);

		const FGaeaScalarField* A = AsScalar(AValue);
		const FGaeaScalarField* B = AsScalar(BValue);
		if (!A || !B || A->Domain != B->Domain)
		{
			Error = TEXT("Reference-fidelity Combine currently requires terrain/scalar inputs sharing one domain.");
			return false;
		}
		const FGaeaScalarField* Mask = AsScalar(Input(Inputs, TEXT("Mask")));
		if (Mask && Mask->Domain != A->Domain)
		{
			Error = TEXT("Combine Mask must share the input domain.");
			return false;
		}

		const float Ratio = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Ratio"), 0.5)), 0.0f, 1.0f);
		const FName Mode = CanonicalCombineMode(Node.GetName(TEXT("Mode"), TEXT("Blend")));
		const FName OutputMode = Node.GetName(TEXT("Output"), TEXT("Clamp"));
		const FName Enhance = Node.GetName(TEXT("Enhance"), TEXT("None"));
		const bool bTerrain = AValue->Type == EGaeaTerrainValueType::Terrain;
		const bool bRawTerrainMultiply = bTerrain && Mode == TEXT("Multiply") && Enhance == TEXT("None");
		FGaeaScalarField Result = *A;

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
		// Gaea's "Enhance Input" modifies each input before the selected blend
		// operation. Applying it to the merged result changes the meaning of Ratio,
		// masks and every non-linear blend mode.
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

		TArray<float> Result01;
		Result01.SetNumUninitialized(Result.Values.Num());
		for (int32 Y = 0; Y < A->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < A->Domain.Dimensions.X; ++X)
			{
				const int32 I = Result.Domain.GetStorageIndex(X, Y);
				const float ARaw = A->AtInterior(X, Y);
				const float BRaw = B->AtInterior(X, Y);
				const float AV = A01[I];
				const float BV = B01[I];
				float V;
				if (bRawTerrainMultiply)
				{
					// EONFORM terrains are signed around sea level. Keep literal zero as an
					// absorbing value for geometric terrain multiplication while retaining
					// the Gaea Ratio and mask routing contract.
					float Raw = FMath::Lerp(ARaw, ARaw * BRaw, Ratio);
					if (Mask)
					{
						const float M = FMath::Clamp(Mask->AtInterior(X, Y), 0.0f, 1.0f);
						Raw = FMath::Lerp(BRaw, ARaw, M);
					}
					V = Raw * 0.5f + 0.5f;
				}
				else
				{
					V = FMath::Lerp(AV, BlendMode(AV, BV, Mode), Ratio);
					// Gaea Combine mask semantics: white selects Input1, black selects Input2.
					if (Mask)
					{
						const float M = FMath::Clamp(Mask->AtInterior(X, Y), 0.0f, 1.0f);
						V = FMath::Lerp(BV, AV, M);
					}
				}
				Result01[I] = V;
			}
		}

		for (int32 Y = 0; Y < Result.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Result.Domain.Dimensions.X; ++X)
			{
				const int32 I = Result.Domain.GetStorageIndex(X, Y);
				float V = Result01[I];
				if (OutputMode == TEXT("Clamp")) V = FMath::Clamp(V, 0.0f, 1.0f);
				else if (OutputMode == TEXT("Extend")) V = 0.5f + 0.5f * FMath::Tanh((V - 0.5f) * 2.0f);
				Result.AtInterior(X, Y) = bTerrain
					? (OutputMode == TEXT("None") ? From01Unclamped(V) : From01(V))
					: (OutputMode == TEXT("Clamp") ? FMath::Clamp(V, 0.0f, 1.0f) : V);
			}
		}

		if (bTerrain)
		{
			Result.Descriptor.Name = GaeaTerrainFieldNames::Height;
			FGaeaTerrainDataset Dataset = AValue->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(Result))) { Error = TEXT("Combine could not publish Height."); return false; }
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), AValue->HeightScale));
		}
		else
		{
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
		}
		return true;
	}

	void RegisterRadialGradient()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::RadialGradient;
		D.DisplayName = TEXT("RadialGradient");
		D.Category = TEXT("Primitive");
		D.Description = TEXT("Generates Gaea-compatible circular falloff geometry at the active graph resolution and physical domain.");
		D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain")));
		D.Parameters = {
			Number(TEXT("Scale"), TEXT("Scale"), 0.5, 0.001, 4.0),
			Number(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0),
			Number(TEXT("X"), TEXT("X"), 0.0, -2.0, 2.0),
			Number(TEXT("Y"), TEXT("Y"), 0.0, -2.0, 2.0)
		};
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateRadialGradient);
	}

	void RegisterVoronoi()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::Voronoi;
		D.DisplayName = TEXT("Voronoi");
		D.Category = TEXT("Primitive");
		D.Description = TEXT("Terrain-oriented geo-variant Voronoi with Gaea forms, internal warp and non-uniform transform controls.");
		D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain")));
		D.Parameters = {
			Number(TEXT("Scale"), TEXT("Scale"), 1.0, 0.001, 16.0, TEXT("Noise")),
			Number(TEXT("Jitter"), TEXT("Jitter"), 1.0, 0.0, 2.0, TEXT("Noise")),
			Choice(TEXT("Function"), TEXT("Function"), TEXT("Euclidean"), { TEXT("Euclidean"), TEXT("Manhattan") }, TEXT("Noise")),
			Choice(TEXT("Form"), TEXT("Form"), TEXT("P"), { TEXT("C"), TEXT("N"), TEXT("R"), TEXT("P"), TEXT("A"), TEXT("S"), TEXT("M"), TEXT("D") }, TEXT("Noise")),
			Number(TEXT("Gain"), TEXT("Gain"), 1.0, 0.0, 4.0, TEXT("Noise")),
			Number(TEXT("Clamp"), TEXT("Clamp"), 1.0, 0.0, 1.0, TEXT("Noise")),
			Integer(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Noise")),
			Choice(TEXT("WarpType"), TEXT("Type"), TEXT("None"), { TEXT("None"), TEXT("Simple"), TEXT("Complex") }, TEXT("Warp")),
			Number(TEXT("WarpFrequency"), TEXT("Frequency"), 1.0, 0.001, 16.0, TEXT("Warp")),
			Number(TEXT("WarpAmplitude"), TEXT("Amplitude"), 0.0, 0.0, 4.0, TEXT("Warp")),
			Integer(TEXT("WarpOctaves"), TEXT("Octaves"), 1, 1, 16, TEXT("Warp")),
			Number(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.001, 16.0, TEXT("Transform")),
			Number(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.001, 16.0, TEXT("Transform")),
			Number(TEXT("X"), TEXT("X"), 0.0, -2.0, 2.0, TEXT("Transform")),
			Number(TEXT("Y"), TEXT("Y"), 0.0, -2.0, 2.0, TEXT("Transform"))
		};
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateVoronoi);
	}

	void RegisterCombine()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::Combine;
		D.DisplayName = TEXT("Combine");
		D.Category = TEXT("Utility");
		D.Description = TEXT("Combines two data sources using Gaea blend modes, input enhancement, output range control, swap and documented mask routing semantics.");
		D.Inputs.Add(Port(TEXT("Input1"), TEXT("Input1"), TEXT("Any")));
		D.Inputs.Add(Port(TEXT("Input2"), TEXT("Input2"), TEXT("Any")));
		D.Inputs.Add(Port(TEXT("Mask"), TEXT("Mask"), TEXT("ScalarField")));
		D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Any")));
		D.Parameters.Add(Number(TEXT("Ratio"), TEXT("Ratio"), 0.5, 0.0, 1.0));
		D.Parameters.Add(Choice(TEXT("Mode"), TEXT("Mode"), TEXT("Blend"), { TEXT("Blend"), TEXT("Add"), TEXT("Screen"), TEXT("Subtract"), TEXT("Difference"), TEXT("Multiply"), TEXT("Divide"), TEXT("Divide2"), TEXT("Max"), TEXT("Min"), TEXT("Hypotenuse"), TEXT("Overlay"), TEXT("Power"), TEXT("Exclusion"), TEXT("Dodge"), TEXT("Burn"), TEXT("SoftLight"), TEXT("HardLight"), TEXT("PinLight"), TEXT("GrainMerge"), TEXT("GrainExtract"), TEXT("Reflect"), TEXT("Glow"), TEXT("Phoenix") }));
		D.Parameters.Add(Choice(TEXT("Output"), TEXT("Output"), TEXT("Clamp"), { TEXT("None"), TEXT("Clamp"), TEXT("Extend") }));
		D.Parameters.Add(Choice(TEXT("Enhance"), TEXT("Enhance Input"), TEXT("None"), { TEXT("None"), TEXT("Autolevel"), TEXT("Equalize") }));
		D.Parameters.Add(Boolean(TEXT("SwapInputs"), TEXT("Swap Inputs"), false));
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateCombine);
	}
}

void RegisterGaeaReferenceFidelityNodes()
{
	using namespace GaeaReferenceFidelity;
	RegisterRadialGradient();
	RegisterVoronoi();
	RegisterCombine();
}
