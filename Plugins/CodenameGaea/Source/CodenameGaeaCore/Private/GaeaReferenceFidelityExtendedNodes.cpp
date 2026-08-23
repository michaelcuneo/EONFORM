#include "GaeaReferenceFidelityExtendedNodes.h"

#include "GaeaGridDomain.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaReferenceFidelityExtended
{
	FGaeaTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FGaeaTerrainPortDescriptor P; P.Name = Name; P.DisplayName = Label; P.DataType = Type; return P;
	}
	FGaeaTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Number; P.DefaultNumber = Default; P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; if (Group) P.Group = Group; return P;
	}
	FGaeaTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Integer; P.DefaultInteger = Default; P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max); if (Group) P.Group = Group; return P;
	}
	FGaeaTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Boolean; P.DefaultBoolean = Default; if (Group) P.Group = Group; return P;
	}
	FGaeaTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Name; P.DefaultName = Default; for (const FName V : Options) P.NameOptions.Add(V); if (Group) P.Group = Group; return P;
	}

	uint32 Hash(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16; return X;
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
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const float FX = X - X0, FY = Y - Y0;
		const float SX = FX * FX * (3.0f - 2.0f * FX), SY = FY * FY * (3.0f - 2.0f * FY);
		const float A = FMath::Lerp(Hash01(X0, Y0, Seed, Salt), Hash01(X0 + 1, Y0, Seed, Salt), SX);
		const float B = FMath::Lerp(Hash01(X0, Y0 + 1, Seed, Salt), Hash01(X0 + 1, Y0 + 1, Seed, Salt), SX);
		return FMath::Lerp(A, B, SY) * 2.0f - 1.0f;
	}
	float Fbm(float X, float Y, float Frequency, int32 Octaves, float Roughness, int32 Seed, uint32 Salt)
	{
		float Sum = 0.0f, Amp = 1.0f, Weight = 0.0f;
		for (int32 I = 0; I < Octaves; ++I)
		{
			Sum += SmoothNoise(X * Frequency, Y * Frequency, Seed + I * 313, Salt + I * 3571u) * Amp;
			Weight += Amp;
			Frequency *= 2.01f;
			Amp *= Roughness;
		}
		return Weight > UE_SMALL_NUMBER ? Sum / Weight : 0.0f;
	}

	const FGaeaTerrainValue* Input(const FGaeaTerrainNodeInputs& Inputs, FName Name)
	{
		const FGaeaTerrainValue* const* P = Inputs.Find(Name); return P ? *P : nullptr;
	}
	const FGaeaScalarField* AsField(const FGaeaTerrainValue* V)
	{
		if (!V || !V->IsValid()) return nullptr;
		if (V->Type == EGaeaTerrainValueType::ScalarField) return &V->ScalarField;
		if (V->Type == EGaeaTerrainValueType::Terrain) return V->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		return nullptr;
	}
	float Sample(const FGaeaScalarField& F, int32 X, int32 Y)
	{
		return F.AtInterior(FMath::Clamp(X, 0, F.Domain.Dimensions.X - 1), FMath::Clamp(Y, 0, F.Domain.Dimensions.Y - 1));
	}
	float MirrorCoord(float V, int32 MaxIndex)
	{
		if (MaxIndex <= 0) return 0.0f;
		const float Period = static_cast<float>(MaxIndex * 2);
		float R = FMath::Fmod(V, Period); if (R < 0.0f) R += Period;
		return R > MaxIndex ? Period - R : R;
	}
	float Bilinear(const FGaeaScalarField& F, float X, float Y, bool bMirror)
	{
		const int32 W = F.Domain.Dimensions.X, H = F.Domain.Dimensions.Y;
		if (bMirror) { X = MirrorCoord(X, W - 1); Y = MirrorCoord(Y, H - 1); }
		else { X = FMath::Clamp(X, 0.0f, static_cast<float>(W - 1)); Y = FMath::Clamp(Y, 0.0f, static_cast<float>(H - 1)); }
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const int32 X1 = FMath::Min(X0 + 1, W - 1), Y1 = FMath::Min(Y0 + 1, H - 1);
		const float TX = X - X0, TY = Y - Y0;
		return FMath::Lerp(FMath::Lerp(F.AtInterior(X0, Y0), F.AtInterior(X1, Y0), TX), FMath::Lerp(F.AtInterior(X0, Y1), F.AtInterior(X1, Y1), TX), TY);
	}
	bool PublishLike(const FGaeaTerrainValue& Prototype, FGaeaScalarField&& Field, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		if (Prototype.Type == EGaeaTerrainValueType::ScalarField)
		{
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Field))); return true;
		}
		if (Prototype.Type == EGaeaTerrainValueType::Terrain)
		{
			Field.Descriptor.Name = GaeaTerrainFieldNames::Height;
			FGaeaTerrainDataset Dataset = Prototype.TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(Field))) { Error = TEXT("Audited node could not publish Height."); return false; }
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Prototype.HeightScale)); return true;
		}
		Error = TEXT("Audited node received unsupported input type."); return false;
	}

	struct FCellSample { float F1 = 9999.0f; float F2 = 9999.0f; float Cell = 0.0f; };
	FCellSample Cell(float X, float Y, int32 Seed)
	{
		const int32 BX = FMath::FloorToInt(X), BY = FMath::FloorToInt(Y);
		FCellSample S;
		for (int32 CY = BY - 2; CY <= BY + 2; ++CY) for (int32 CX = BX - 2; CX <= BX + 2; ++CX)
		{
			const float FX = CX + 0.5f + (Hash01(CX, CY, Seed, 0x11u) - 0.5f) * 0.92f;
			const float FY = CY + 0.5f + (Hash01(CY, CX, Seed, 0x41u) - 0.5f) * 0.92f;
			const float DX = X - FX, DY = Y - FY, D = FMath::Sqrt(DX * DX + DY * DY);
			if (D < S.F1) { S.F2 = S.F1; S.F1 = D; S.Cell = Hash01(CX, CY, Seed, 0x71u); }
			else if (D < S.F2) S.F2 = D;
		}
		return S;
	}
	float WarpSource(float U, float V, FName Source, int32 Complexity, float Roughness, int32 Seed, uint32 Salt)
	{
		if (Source == TEXT("Perlin FBM")) return Fbm(U, V, 3.0f, Complexity, Roughness, Seed, Salt);
		const FCellSample S = Cell(U * 10.0f, V * 10.0f, Seed + static_cast<int32>(Salt));
		const float P = FMath::Clamp(1.0f - S.F1, 0.0f, 1.0f);
		const float Edge = FMath::Clamp(1.0f - (S.F2 - S.F1) * 2.0f, 0.0f, 1.0f);
		float V0 = P;
		if (Source == TEXT("Voronoi R")) V0 = FMath::Pow(Edge, 1.5f);
		else if (Source == TEXT("Voronoi S")) V0 = FMath::Pow(P, 2.4f) * FMath::Clamp((S.F2 - S.F1) * 2.0f, 0.0f, 1.0f);
		else if (Source == TEXT("Voronoi M")) V0 = P * FMath::Lerp(0.5f, 1.25f, S.Cell);
		else if (Source == TEXT("Voronoi D")) V0 = FMath::Pow(Edge, 2.2f);
		else if (Source == TEXT("Voronoi A")) V0 = FMath::Lerp(P, P * FMath::Lerp(0.5f, 1.25f, S.Cell), 0.5f);
		return V0 * 2.0f - 1.0f;
	}

	bool EvaluateWarp(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* SourceValue = Input(Inputs, TEXT("Input"));
		const FGaeaScalarField* Source = AsField(SourceValue);
		if (!SourceValue || !Source) { Error = TEXT("Warp requires Input."); return false; }
		const FGaeaScalarField* ModulationField = AsField(Input(Inputs, TEXT("Modulation")));
		if (ModulationField && ModulationField->Domain != Source->Domain) { Error = TEXT("Warp Modulation must share the Input domain."); return false; }

		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.35)), 0.005f, 4.0f);
		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.2)), 0.0f, 2.0f);
		const float ZScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ZScale"), 1.0)), 0.0f, 4.0f);
		const FName SourceType = Node.GetName(TEXT("WarpSource"), TEXT("Perlin FBM"));
		const float Perturbation = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Perturbation"), 0.0)), 0.0f, 1.0f);
		const int32 Complexity = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Complexity"), 4)), 1, 12);
		const float Roughness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Roughness"), 0.5)), 0.05f, 0.95f);
		const bool bNormalized = Node.GetBool(TEXT("Normalized"), true);
		const FName EdgeBehaviour = Node.GetName(TEXT("EdgeBehaviour"), TEXT("Mirror"));
		const float Modulation = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Modulation"), 1.0)), 0.0f, 2.0f);
		const float ModulationDirection = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("ModulationDirection"), 0.0)));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const int32 Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 1)), 1, 16);
		const FName Mode = Node.GetName(TEXT("Mode"), TEXT("Vector Field"));
		const bool bMirror = EdgeBehaviour == TEXT("Mirror");
		const float MinDim = static_cast<float>(FMath::Min(Source->Domain.Dimensions.X, Source->Domain.Dimensions.Y));
		const float PixelStrength = Strength * MinDim * 0.075f;
		const float Frequency = 1.6f / FMath::Max(Size, 0.005f);
		FGaeaScalarField Current = *Source;

		for (int32 Iter = 0; Iter < Iterations; ++Iter)
		{
			FGaeaScalarField Next = Current;
			const float IterScale = Mode == TEXT("Bitmap") ? 1.0f : (Mode == TEXT("Vector Field Integral") ? 0.72f : 0.86f);
			for (int32 Y = 0; Y < Current.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Current.Domain.Dimensions.X; ++X)
			{
				const float U = Current.Domain.Dimensions.X > 1 ? static_cast<float>(X) / (Current.Domain.Dimensions.X - 1) - 0.5f : 0.0f;
				const float V = Current.Domain.Dimensions.Y > 1 ? static_cast<float>(Y) / (Current.Domain.Dimensions.Y - 1) - 0.5f : 0.0f;
				float WX = WarpSource(U * Frequency, V * Frequency, SourceType, Complexity, Roughness, Seed + Iter * 101, 0x211u);
				float WY = WarpSource(U * Frequency, V * Frequency, SourceType, Complexity, Roughness, Seed + Iter * 101, 0x917u);
				if (Perturbation > UE_SMALL_NUMBER)
				{
					WX += Fbm(U, V, Frequency * 3.7f, 3, 0.55f, Seed + 517, 0x53u) * Perturbation;
					WY += Fbm(U, V, Frequency * 3.7f, 3, 0.55f, Seed + 911, 0x79u) * Perturbation;
				}
				if (bNormalized)
				{
					const float L = FMath::Sqrt(WX * WX + WY * WY);
					if (L > UE_SMALL_NUMBER) { WX /= L; WY /= L; }
				}
				float LocalMod = 1.0f;
				if (ModulationField)
				{
					const float M = FMath::Clamp(ModulationField->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
					const float Directed = 0.5f + 0.5f * (WX * FMath::Cos(ModulationDirection) + WY * FMath::Sin(ModulationDirection));
					LocalMod = FMath::Lerp(1.0f, M * Directed, Modulation);
				}
				const float HeightResponse = FMath::Lerp(1.0f, FMath::Clamp(FMath::Abs(Current.AtInterior(X, Y)), 0.0f, 1.0f), ZScale * 0.25f);
				const float D = PixelStrength * IterScale * LocalMod * HeightResponse;
				Next.AtInterior(X, Y) = Bilinear(Current, X - WX * D, Y - WY * D, bMirror);
			}
			Current = MoveTemp(Next);
		}
		return PublishLike(*SourceValue, MoveTemp(Current), Out, Error);
	}

	FGaeaGridDomain BuildSourceDomain(const FGaeaTerrainEvaluationContext& Context)
	{
		const int32 RX = FMath::Clamp(Context.TargetResolution.X > 1 ? Context.TargetResolution.X : 257, 2, 4097);
		const int32 RY = FMath::Clamp(Context.TargetResolution.Y > 1 ? Context.TargetResolution.Y : RX, 2, 4097);
		double W = 100000.0, H = 100000.0;
		if (Context.PhysicalMetrics.HasWorldDimensions()) { W = Context.PhysicalMetrics.WorldWidthMeters * 100.0; H = Context.PhysicalMetrics.WorldDepthMeters * 100.0; }
		return FGaeaGridDomain::Make(FIntPoint(RX, RY), FVector2d(-W * 0.5, -H * 0.5), FVector2d(W * 0.5, H * 0.5));
	}
	float SourceHeightScale(const FGaeaTerrainEvaluationContext& Context)
	{
		return Context.PhysicalMetrics.HasElevationScale() ? static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0) : FMath::Max(Context.HeightScale, 1.0f);
	}
	FGaeaScalarField MakeHeight(const FGaeaGridDomain& Domain)
	{
		FGaeaFieldDescriptor D; D.Name = GaeaTerrainFieldNames::Height; D.Unit = EGaeaFieldUnit::Normalized; D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField F; F.Initialize(Domain, D, 0.0f); return F;
	}

	float RockStamp(float DX, float DY, float Radius, FName Style, float Variety)
	{
		const float AX = FMath::Abs(DX) / FMath::Max(Radius, 0.001f), AY = FMath::Abs(DY) / FMath::Max(Radius, 0.001f);
		float D = FMath::Sqrt(AX * AX + AY * AY);
		if (Style == TEXT("B")) D = FMath::Max(AX, AY) * FMath::Lerp(0.92f, 1.18f, Variety);
		else if (Style == TEXT("C")) D = FMath::Pow(FMath::Pow(AX, 1.45f) + FMath::Pow(AY, 1.45f), 1.0f / 1.45f);
		if (D >= 1.0f) return 0.0f;
		float V = FMath::Pow(1.0f - D * D, Style == TEXT("A") ? 1.45f : (Style == TEXT("B") ? 0.78f : 1.02f));
		if (Style == TEXT("C")) V *= 0.78f + 0.22f * FMath::Cos((DX + DY) / FMath::Max(Radius, 0.001f) * PI * 2.0f);
		return FMath::Max(V, 0.0f);
	}

	bool EvaluateRockNoise(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaGridDomain Domain = BuildSourceDomain(Context);
		if (!Domain.IsValid()) { Error = TEXT("RockNoise produced invalid domain."); return false; }
		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.5)), 0.02f, 4.0f);
		const float Variety = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Variety"), 0.5)), 0.0f, 1.0f);
		const int32 Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 4)), 1, 8);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const FName Style = Node.GetName(TEXT("Style"), TEXT("A"));
		FGaeaScalarField Height = MakeHeight(Domain);
		const int32 W = Domain.Dimensions.X, H = Domain.Dimensions.Y;
		for (int32 O = 0; O < Octaves; ++O)
		{
			const float Cells = FMath::Lerp(7.0f, 28.0f, 1.0f / FMath::Max(Size, 0.02f)) * FMath::Pow(2.0f, O);
			const float Amp = FMath::Pow(0.62f, O);
			for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
			{
				const float U = static_cast<float>(X) / FMath::Max(W - 1, 1) * Cells;
				const float V = static_cast<float>(Y) / FMath::Max(H - 1, 1) * Cells;
				const int32 BX = FMath::FloorToInt(U), BY = FMath::FloorToInt(V);
				float Stamp = 0.0f;
				for (int32 CY = BY - 1; CY <= BY + 1; ++CY) for (int32 CX = BX - 1; CX <= BX + 1; ++CX)
				{
					const float Chance = Hash01(CX, CY, Seed + O * 991, 0x27u);
					if (Chance < FMath::Lerp(0.58f, 0.28f, Variety)) continue;
					const float PX = CX + 0.5f + (Hash01(CX, CY, Seed, 0x31u) - 0.5f) * 0.78f;
					const float PY = CY + 0.5f + (Hash01(CY, CX, Seed, 0x47u) - 0.5f) * 0.78f;
					const float Radius = FMath::Lerp(0.16f, 0.46f, Hash01(CX, CY, Seed + O * 101, 0x59u)) * FMath::Lerp(0.72f, 1.32f, Variety);
					Stamp = FMath::Max(Stamp, RockStamp(U - PX, V - PY, Radius, Style, Variety));
				}
				Height.AtInterior(X, Y) = FMath::Max(Height.AtInterior(X, Y), Stamp * Amp);
			}
		}
		FGaeaTerrainDataset Dataset; if (!Dataset.SetScalarField(MoveTemp(Height))) { Error = TEXT("RockNoise could not publish Height."); return false; }
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), SourceHeightScale(Context)));
		return true;
	}

	bool EvaluateGroundTexture(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* V = Input(Inputs, TEXT("Terrain")); const FGaeaScalarField* Source = AsField(V);
		if (!V || V->Type != EGaeaTerrainValueType::Terrain || !Source) { Error = TEXT("GroundTexture requires Terrain input."); return false; }
		const FName Method = Node.GetName(TEXT("Method"), TEXT("Rough"));
		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.5)), 0.0f, 2.0f);
		const float Coverage = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Coverage"), 0.65)), 0.0f, 1.0f);
		const float Density = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Density"), 0.5)), 0.01f, 2.0f);
		FGaeaScalarField R = *Source;
		const int32 W = R.Domain.Dimensions.X, H = R.Domain.Dimensions.Y;
		const float Frequency = FMath::Lerp(35.0f, 180.0f, Density * 0.5f);
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
		{
			const float U = static_cast<float>(X) / FMath::Max(W - 1, 1), V0 = static_cast<float>(Y) / FMath::Max(H - 1, 1);
			const float CoverNoise = 0.5f + 0.5f * Fbm(U, V0, Frequency * 0.16f, 3, 0.55f, 4751, 0x91u);
			const float Mask = FMath::SmoothStep(1.0f - Coverage, FMath::Min(1.0f, 1.08f - Coverage * 0.25f), CoverNoise);
			const float N1 = Fbm(U, V0, Frequency, 4, 0.52f, 7127, 0xB1u);
			const float N2 = Fbm(U, V0, Frequency * 2.9f, 3, 0.48f, 9419, 0xC1u);
			float Detail = N1 * 0.7f + N2 * 0.3f;
			if (Method == TEXT("Rocky")) Detail = (1.0f - FMath::Abs(Detail)) * 2.0f - 1.0f;
			else if (Method == TEXT("Harsh")) Detail = FMath::Sign(Detail) * FMath::Pow(FMath::Abs(Detail), 0.55f);
			R.AtInterior(X, Y) = FMath::Clamp(Source->AtInterior(X, Y) + Detail * Mask * Strength * 0.035f, -1.0f, 1.0f);
		}
		return PublishLike(*V, MoveTemp(R), Out, Error);
	}

	bool EvaluateStratify(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* V = Input(Inputs, TEXT("Terrain")); const FGaeaScalarField* Source = AsField(V);
		if (!V || V->Type != EGaeaTerrainValueType::Terrain || !Source) { Error = TEXT("Stratify requires Terrain input."); return false; }
		const float Spacing = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Spacing"), 0.5)), 0.02f, 4.0f);
		const int32 Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 4)), 1, 8);
		const float Intensity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Intensity"), 0.5)), 0.0f, 1.5f);
		const float Shape = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shape"), 0.5)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Tilt = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("TiltAmount"), 0.0)), -1.0f, 1.0f);
		const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)));
		FGaeaScalarField R = *Source;
		const int32 W = R.Domain.Dimensions.X, H = R.Domain.Dimensions.Y;
		const float LayerFrequency = FMath::Lerp(48.0f, 6.0f, FMath::Clamp(Spacing / 4.0f, 0.0f, 1.0f));
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
		{
			const float U = static_cast<float>(X) / FMath::Max(W - 1, 1) - 0.5f, V0 = static_cast<float>(Y) / FMath::Max(H - 1, 1) - 0.5f;
			const float Directional = U * FMath::Cos(Direction) + V0 * FMath::Sin(Direction);
			const float Breakup = Fbm(U, V0, 3.5f, Octaves, 0.56f, Seed, 0xD1u);
			const float Zone = FMath::SmoothStep(-0.15f, 0.32f, Fbm(U, V0, 1.7f, 3, 0.55f, Seed + 503, 0xE1u));
			const float H01 = FMath::Clamp(Source->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
			const float Phase = (H01 + Directional * Tilt * 0.24f + Breakup * 0.035f) * LayerFrequency;
			float Layer = 1.0f - 2.0f * FMath::Abs(FMath::Frac(Phase) - 0.5f);
			Layer = FMath::Pow(FMath::Clamp(Layer, 0.0f, 1.0f), FMath::Lerp(0.65f, 4.5f, Shape));
			const float Delta = (Layer - 0.42f) * Intensity * Zone * 0.055f;
			R.AtInterior(X, Y) = FMath::Clamp(Source->AtInterior(X, Y) + Delta, -1.0f, 1.0f);
		}
		return PublishLike(*V, MoveTemp(R), Out, Error);
	}

	void RegisterWarp()
	{
		FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::Warp; D.DisplayName = TEXT("Warp"); D.Category = TEXT("Modify");
		D.Description = TEXT("Gaea-compatible vector-field warp with selectable source, modulation and iterative modes.");
		D.Inputs.Add(Port(TEXT("Input"), TEXT("Input"), TEXT("Any")));
		D.Inputs.Add(Port(TEXT("Modulation"), TEXT("Modulation"), TEXT("ScalarField")));
		D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Any")));
		D.Parameters = {
			Num(TEXT("Size"), TEXT("Size"), 0.35, 0.005, 4.0), Num(TEXT("Strength"), TEXT("Strength"), 0.2, 0.0, 2.0), Num(TEXT("ZScale"), TEXT("Z Scale"), 1.0, 0.0, 4.0),
			Choice(TEXT("WarpSource"), TEXT("Warp Source"), TEXT("Perlin FBM"), { TEXT("Perlin FBM"), TEXT("Voronoi R"), TEXT("Voronoi P"), TEXT("Voronoi A"), TEXT("Voronoi S"), TEXT("Voronoi M"), TEXT("Voronoi D") }),
			Num(TEXT("Perturbation"), TEXT("Perturbation"), 0.0, 0.0, 1.0), Int(TEXT("Complexity"), TEXT("Complexity"), 4, 1, 12), Num(TEXT("Roughness"), TEXT("Roughness"), 0.5, 0.05, 0.95), Bool(TEXT("Normalized"), TEXT("Normalized"), true),
			Choice(TEXT("EdgeBehaviour"), TEXT("Edge Behaviour"), TEXT("Mirror"), { TEXT("Edge"), TEXT("Mirror") }), Num(TEXT("Modulation"), TEXT("Modulation"), 1.0, 0.0, 2.0), Num(TEXT("ModulationDirection"), TEXT("Modulation Direction"), 0.0, -360.0, 360.0),
			Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647), Int(TEXT("Iterations"), TEXT("Iterations"), 1, 1, 16), Choice(TEXT("Mode"), TEXT("Mode"), TEXT("Vector Field"), { TEXT("Bitmap"), TEXT("Vector Field"), TEXT("Vector Field Integral") })
		};
		FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateWarp);
	}
	void RegisterRockNoise()
	{
		FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::RockNoise; D.DisplayName = TEXT("RockNoise"); D.Category = TEXT("Surface");
		D.Description = TEXT("Generates a flat multi-octave rock field for Transpose Embed/Insert workflows."); D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain")));
		D.Parameters = { Num(TEXT("Size"), TEXT("Size"), 0.5, 0.02, 4.0), Num(TEXT("Variety"), TEXT("Variety"), 0.5, 0.0, 1.0), Int(TEXT("Octaves"), TEXT("Octaves"), 4, 1, 8), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647), Choice(TEXT("Style"), TEXT("Style"), TEXT("A"), { TEXT("A"), TEXT("B"), TEXT("C") }) };
		FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateRockNoise);
	}
	void RegisterGroundTexture()
	{
		FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::GroundTexture; D.DisplayName = TEXT("GroundTexture"); D.Category = TEXT("Surface"); D.Description = TEXT("Adds superficial Harsh, Rocky or Rough detail with controllable strength, coverage and density.");
		D.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain")));
		D.Parameters = { Choice(TEXT("Method"), TEXT("Method"), TEXT("Rough"), { TEXT("Harsh"), TEXT("Rocky"), TEXT("Rough") }), Num(TEXT("Strength"), TEXT("Strength"), 0.5, 0.0, 2.0), Num(TEXT("Coverage"), TEXT("Coverage"), 0.65, 0.0, 1.0), Num(TEXT("Density"), TEXT("Density"), 0.5, 0.01, 2.0) };
		FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateGroundTexture);
	}
	void RegisterStratify()
	{
		FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::Stratify; D.DisplayName = TEXT("Stratify"); D.Category = TEXT("Surface"); D.Description = TEXT("Creates broken non-linear rock strata with independent local zones and geological tilt.");
		D.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain")));
		D.Parameters = { Num(TEXT("Spacing"), TEXT("Spacing"), 0.5, 0.02, 4.0), Int(TEXT("Octaves"), TEXT("Octaves"), 4, 1, 8), Num(TEXT("Intensity"), TEXT("Intensity"), 0.5, 0.0, 1.5), Num(TEXT("Shape"), TEXT("Shape"), 0.5, 0.0, 1.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647), Num(TEXT("TiltAmount"), TEXT("Tilt Amount"), 0.0, -1.0, 1.0), Num(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0) };
		FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateStratify);
	}
}

void RegisterGaeaReferenceFidelityExtendedNodes()
{
	using namespace GaeaReferenceFidelityExtended;
	RegisterWarp();
	RegisterRockNoise();
	RegisterGroundTexture();
	RegisterStratify();
}
