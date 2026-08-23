#include "GaeaTerrainProceduralOps.h"

#include "GaeaTerrainFieldNames.h"
#include "Math/RandomStream.h"

namespace GaeaTerrainProceduralOps
{
	namespace
	{
		// Gaea's RawNoise implementation uses FastNoiseSIMD. Its packed constants
		// still hide the exact product used by SetFrequency and its perturb amplitude.
		// Keep those two unresolved calibration values isolated here; all surrounding
		// coordinate, scale, cellular-return and 0..1 mapping semantics are now explicit.
		constexpr float RawNoiseFeatureCycles = 12.0f;
		constexpr float RawNoisePerturbFraction = 0.06f;

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

		float Fade(float T)
		{
			return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
		}

		FVector2D Gradient(int32 X, int32 Y, int32 Seed, uint32 Salt)
		{
			const float Angle = Hash01(X, Y, Seed, Salt) * 2.0f * PI;
			return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
		}

		float Perlin(float X, float Y, int32 Seed, uint32 Salt)
		{
			const int32 X0 = FMath::FloorToInt(X);
			const int32 Y0 = FMath::FloorToInt(Y);
			const int32 X1 = X0 + 1;
			const int32 Y1 = Y0 + 1;
			const float TX = X - static_cast<float>(X0);
			const float TY = Y - static_cast<float>(Y0);
			const float U = Fade(TX);
			const float V = Fade(TY);

			auto Dot = [&](int32 GX, int32 GY, float DX, float DY)
			{
				const FVector2D G = Gradient(GX, GY, Seed, Salt);
				return G.X * DX + G.Y * DY;
			};

			const float A = FMath::Lerp(Dot(X0, Y0, TX, TY), Dot(X1, Y0, TX - 1.0f, TY), U);
			const float B = FMath::Lerp(Dot(X0, Y1, TX, TY - 1.0f), Dot(X1, Y1, TX - 1.0f, TY - 1.0f), U);
			return FMath::Clamp(FMath::Lerp(A, B, V) * 1.41421356f, -1.0f, 1.0f);
		}

		float PerlinFractal(float X, float Y, int32 Octaves, float Gain, int32 Seed, uint32 Salt)
		{
			float Frequency = 1.0f;
			float Amplitude = 1.0f;
			float Sum = 0.0f;
			float Weight = 0.0f;
			for (int32 I = 0; I < FMath::Max(Octaves, 1); ++I)
			{
				Sum += Perlin(X * Frequency, Y * Frequency, Seed + I, Salt) * Amplitude;
				Weight += Amplitude;
				Frequency *= 2.0f;
				Amplitude *= Gain;
			}
			return Weight > UE_SMALL_NUMBER ? Sum / Weight : 0.0f;
		}

		float PerlinBillow(float X, float Y, int32 Octaves, float Gain, int32 Seed, uint32 Salt)
		{
			float Frequency = 1.0f;
			float Amplitude = 1.0f;
			float Sum = 0.0f;
			float Weight = 0.0f;
			for (int32 I = 0; I < FMath::Max(Octaves, 1); ++I)
			{
				const float N = FMath::Abs(Perlin(X * Frequency, Y * Frequency, Seed + I, Salt)) * 2.0f - 1.0f;
				Sum += N * Amplitude;
				Weight += Amplitude;
				Frequency *= 2.0f;
				Amplitude *= Gain;
			}
			return Weight > UE_SMALL_NUMBER ? Sum / Weight : 0.0f;
		}

		float PerlinRigidMulti(float X, float Y, int32 Octaves, float Gain, int32 Seed, uint32 Salt)
		{
			float Frequency = 1.0f;
			float Amplitude = 1.0f;
			float Result = 1.0f - FMath::Abs(Perlin(X, Y, Seed, Salt));
			for (int32 I = 1; I < FMath::Max(Octaves, 1); ++I)
			{
				Frequency *= 2.0f;
				Amplitude *= Gain;
				Result -= (1.0f - FMath::Abs(Perlin(X * Frequency, Y * Frequency, Seed + I, Salt))) * Amplitude;
			}
			return Result;
		}

		float MirrorCoord(float V, int32 MaxIndex)
		{
			if (MaxIndex <= 0) return 0.0f;
			const float Period = static_cast<float>(MaxIndex * 2);
			float R = FMath::Fmod(V, Period);
			if (R < 0.0f) R += Period;
			return R > MaxIndex ? Period - R : R;
		}

		float Bilinear(const FGaeaScalarField& Field, float X, float Y, EEdgeBehaviour Edge)
		{
			const int32 W = Field.Domain.Dimensions.X;
			const int32 H = Field.Domain.Dimensions.Y;
			if (Edge == EEdgeBehaviour::Mirror)
			{
				X = MirrorCoord(X, W - 1);
				Y = MirrorCoord(Y, H - 1);
			}
			else
			{
				X = FMath::Clamp(X, 0.0f, static_cast<float>(W - 1));
				Y = FMath::Clamp(Y, 0.0f, static_cast<float>(H - 1));
			}
			const int32 X0 = FMath::FloorToInt(X);
			const int32 Y0 = FMath::FloorToInt(Y);
			const int32 X1 = FMath::Min(X0 + 1, W - 1);
			const int32 Y1 = FMath::Min(Y0 + 1, H - 1);
			const float TX = X - static_cast<float>(X0);
			const float TY = Y - static_cast<float>(Y0);
			return FMath::Lerp(
				FMath::Lerp(Field.AtInterior(X0, Y0), Field.AtInterior(X1, Y0), TX),
				FMath::Lerp(Field.AtInterior(X0, Y1), Field.AtInterior(X1, Y1), TX),
				TY);
		}

		struct FVoronoiSample
		{
			float F1 = TNumericLimits<float>::Max();
			float F2 = TNumericLimits<float>::Max();
			float Cell = 0.0f;
			float FeatureX = 0.0f;
			float FeatureY = 0.0f;
		};

		float CellularDistance(float DX, float DY, FName Function)
		{
			const float Euclidean = DX * DX + DY * DY;
			const float Manhattan = FMath::Abs(DX) + FMath::Abs(DY);
			if (Function == TEXT("Manhattan")) return Manhattan;
			if (Function == TEXT("Natural")) return Euclidean * Manhattan;
			return Euclidean;
		}

		FVoronoiSample SampleVoronoi(float X, float Y, float Jitter, FName Function, int32 Seed)
		{
			const int32 BX = FMath::FloorToInt(X);
			const int32 BY = FMath::FloorToInt(Y);
			FVoronoiSample S;
			for (int32 CY = BY - 2; CY <= BY + 2; ++CY)
			{
				for (int32 CX = BX - 2; CX <= BX + 2; ++CX)
				{
					const float Angle = Hash01(CX, CY, Seed, 0x17u) * 2.0f * PI;
					const float Radius = Jitter * (0.72f + Hash01(CY, CX, Seed, 0x91u) * 0.28f);
					const float FeatureX = static_cast<float>(CX) + FMath::Cos(Angle) * Radius;
					const float FeatureY = static_cast<float>(CY) + FMath::Sin(Angle) * Radius;
					const float D = CellularDistance(X - FeatureX, Y - FeatureY, Function);
					if (D < S.F1)
					{
						S.F2 = S.F1;
						S.F1 = D;
						S.Cell = Hash01(CX, CY, Seed, 0x71u) * 2.0f - 1.0f;
						S.FeatureX = FeatureX;
						S.FeatureY = FeatureY;
					}
					else if (D < S.F2)
					{
						S.F2 = D;
					}
				}
			}
			return S;
		}

		float VoronoiRawForm(float X, float Y, const FVoronoiSample& S, FName Form, float Jitter, FName Function, int32 Seed, float LookupFrequency)
		{
			// RawNoise maps FastNoiseSIMD's signed/raw cellular result with
			// 0.5 + value * 0.5. These letters are Gaea's enum-to-FNSIMD mapping:
			// C=CellValue, R=Distance, A=Distance2, P=Distance2Add,
			// S=Distance2Mul, M=Distance2Div, D=NoiseLookup, N=Distance2Cave.
			if (Form == TEXT("C")) return S.Cell;
			if (Form == TEXT("R")) return S.F1;
			if (Form == TEXT("A")) return S.F2;
			if (Form == TEXT("P")) return S.F1 + S.F2;
			if (Form == TEXT("S")) return S.F1 * S.F2;
			if (Form == TEXT("M")) return S.F1 / FMath::Max(S.F2, UE_SMALL_NUMBER);
			if (Form == TEXT("D")) return Perlin(S.FeatureX * LookupFrequency, S.FeatureY * LookupFrequency, Seed, 0x4d31u);
			if (Form == TEXT("N"))
			{
				const float C0 = S.F1 / FMath::Max(S.F2, UE_SMALL_NUMBER);
				const FVoronoiSample S1 = SampleVoronoi(X + 0.5f, Y + 0.5f, Jitter, Function, Seed + 1);
				const float C1 = S1.F1 / FMath::Max(S1.F2, UE_SMALL_NUMBER);
				return FMath::Min(C0, C1);
			}
			return S.F1 + S.F2;
		}

		FName CanonicalWarpType(FName Type)
		{
			if (Type == TEXT("PerlinFBM")) return TEXT("Perlin FBM");
			if (Type == TEXT("VoronoiR")) return TEXT("Voronoi R");
			if (Type == TEXT("VoronoiP")) return TEXT("Voronoi P");
			return Type;
		}

		float WarpNoise(float U, float V, FName Type, int32 Octaves, float Roughness, int32 Seed, uint32 Salt, float Jitter)
		{
			Type = CanonicalWarpType(Type);
			if (Type == TEXT("Perlin FBM")) return PerlinFractal(U, V, Octaves, Roughness, Seed, Salt);
			const FVoronoiSample S = SampleVoronoi(U, V, Jitter, TEXT("Euclidean"), Seed + static_cast<int32>(Salt));
			const FName Form = Type == TEXT("Voronoi R") ? TEXT("R")
				: Type == TEXT("Voronoi A") ? TEXT("A")
				: Type == TEXT("Voronoi S") ? TEXT("S")
				: Type == TEXT("Voronoi M") ? TEXT("M")
				: Type == TEXT("Voronoi D") ? TEXT("D")
				: TEXT("P");
			const float Raw = VoronoiRawForm(U, V, S, Form, Jitter, TEXT("Euclidean"), Seed, 0.2f);
			return FMath::Clamp(Raw, -1.0f, 1.0f);
		}

		float PerturbNoise(float RawX, float RawY, FName WarpType, float Frequency, int32 Octaves, int32 Seed, uint32 Salt)
		{
			if (WarpType == TEXT("None")) return 0.0f;
			const float PX = RawX * Frequency;
			const float PY = RawY * Frequency;
			if (WarpType == TEXT("Simple")) return Perlin(PX, PY, Seed, Salt);
			return PerlinFractal(PX, PY, FMath::Max(Octaves, 1), 0.5f, Seed, Salt);
		}

		void MakeHeightDescriptor(FGaeaFieldDescriptor& Descriptor)
		{
			Descriptor.Name = GaeaTerrainFieldNames::Height;
			Descriptor.Unit = EGaeaFieldUnit::Normalized;
			Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		}
	}

	bool GenerateVoronoi(const FGaeaGridDomain& Domain, const FVoronoiSettings& Settings, FGaeaScalarField& OutField, FString* OutError)
	{
		if (!Domain.IsValid())
		{
			if (OutError) *OutError = TEXT("Voronoi requires a valid domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		MakeHeightDescriptor(Descriptor);
		OutField.Initialize(Domain, Descriptor, 0.0f);
		const int32 W = Domain.Dimensions.X;
		const int32 H = Domain.Dimensions.Y;
		const float Resolution = static_cast<float>(W);
		const float Scale = FMath::Clamp(Settings.Scale, 0.0001f, 4.0f);
		const float MainFrequency = Scale * RawNoiseFeatureCycles / FMath::Max(Resolution, 1.0f);
		const float LookupFrequency = Scale;
		const float PerturbPixels = Settings.WarpAmplitude * Resolution * RawNoisePerturbFraction;

		for (int32 Y = 0; Y < H; ++Y)
		{
			const float RawYBase = (H > 1 ? static_cast<float>(Y) / static_cast<float>(H - 1) - 0.5f : 0.0f) * Resolution;
			for (int32 X = 0; X < W; ++X)
			{
				const float RawXBase = (W > 1 ? static_cast<float>(X) / static_cast<float>(W - 1) - 0.5f : 0.0f) * Resolution;
				float RawX = RawXBase;
				float RawY = RawYBase;

				if (Settings.WarpType != TEXT("None") && Settings.WarpAmplitude > UE_SMALL_NUMBER)
				{
					const float WX = PerturbNoise(RawXBase, RawYBase, Settings.WarpType, Settings.WarpFrequency, Settings.WarpOctaves, Settings.Seed + 1201, 0x31u);
					const float WY = PerturbNoise(RawXBase, RawYBase, Settings.WarpType, Settings.WarpFrequency, Settings.WarpOctaves, Settings.Seed + 2707, 0x57u);
					RawX += WX * PerturbPixels;
					RawY += WY * PerturbPixels;
				}

				const float PX = (RawX * MainFrequency + Settings.X) * Settings.ScaleX;
				const float PY = (RawY * MainFrequency + Settings.Y) * Settings.ScaleY;
				const FVoronoiSample S = SampleVoronoi(PX, PY, Settings.Jitter, Settings.Function, Settings.Seed);
				const float Raw = VoronoiRawForm(PX, PY, S, Settings.Form, Settings.Jitter, Settings.Function, Settings.Seed, LookupFrequency);

				// Noises.Voronoi exposes Gain, but the supplied implementation does not
				// forward it to RawNoise.Voronoi. Preserve that behavior deliberately.
				OutField.AtInterior(X, Y) = 0.5f + Raw * 0.5f;
			}
		}

		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	bool GeneratePerlin(const FGaeaGridDomain& Domain, const FPerlinSettings& Settings, FGaeaScalarField& OutField, FString* OutError)
	{
		if (!Domain.IsValid())
		{
			if (OutError) *OutError = TEXT("Perlin requires a valid domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		MakeHeightDescriptor(Descriptor);
		OutField.Initialize(Domain, Descriptor, 0.0f);
		const int32 W = Domain.Dimensions.X;
		const int32 H = Domain.Dimensions.Y;
		const float Resolution = static_cast<float>(W);

		// RawNoise.Perlin explicitly does Scale = 1 - Scale before SetFrequency.
		const float ScaleTerm = FMath::Max(1.0f - FMath::Clamp(Settings.Scale, 0.0f, 1.0f), 0.0001f);
		const float MainFrequency = ScaleTerm * RawNoiseFeatureCycles / FMath::Max(Resolution, 1.0f);
		const float PerturbPixels = Settings.WarpAmplitude * Resolution * RawNoisePerturbFraction;
		const int32 Octaves = FMath::Clamp(Settings.Octaves, 1, 14);
		const float Gain = FMath::Clamp(Settings.Gain, 0.0f, 1.0f);

		for (int32 Y = 0; Y < H; ++Y)
		{
			const float RawYBase = (H > 1 ? static_cast<float>(Y) / static_cast<float>(H - 1) - 0.5f : 0.0f) * Resolution;
			for (int32 X = 0; X < W; ++X)
			{
				const float RawXBase = (W > 1 ? static_cast<float>(X) / static_cast<float>(W - 1) - 0.5f : 0.0f) * Resolution;
				float RawX = RawXBase;
				float RawY = RawYBase;

				if (Settings.WarpType != TEXT("None") && Settings.WarpAmplitude > UE_SMALL_NUMBER)
				{
					const float WX = PerturbNoise(RawXBase, RawYBase, Settings.WarpType, Settings.WarpFrequency, Settings.WarpOctaves, Settings.Seed + 1301, 0x51u);
					const float WY = PerturbNoise(RawXBase, RawYBase, Settings.WarpType, Settings.WarpFrequency, Settings.WarpOctaves, Settings.Seed + 2903, 0x79u);
					RawX += WX * PerturbPixels;
					RawY += WY * PerturbPixels;
				}

				const float PX = (RawX * MainFrequency + Settings.X) * Settings.ScaleX;
				const float PY = (RawY * MainFrequency + Settings.Y) * Settings.ScaleY;
				float Raw;
				if (Settings.Type == TEXT("Ridged")) Raw = PerlinRigidMulti(PX, PY, Octaves, Gain, Settings.Seed, 0x151u);
				else if (Settings.Type == TEXT("Billowy")) Raw = PerlinBillow(PX, PY, Octaves, Gain, Settings.Seed, 0x151u);
				else Raw = PerlinFractal(PX, PY, Octaves, Gain, Settings.Seed, 0x151u);

				OutField.AtInterior(X, Y) = 0.5f + Raw * 0.5f;
			}
		}

		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	bool ApplyTerrace(const FGaeaScalarField& Source, int32 NumTerraces, float Uniformity, float Steepness, float Intensity, int32 Seed, bool bForceZero, FGaeaScalarField& OutField, FString* OutError)
	{
		if (!Source.IsValid())
		{
			if (OutError) *OutError = TEXT("Terrace requires a valid source field.");
			return false;
		}
		const int32 Num = FMath::Max(NumTerraces, 3);
		TArray<float> Levels;
		Levels.SetNumZeroed(bForceZero ? Num + 1 : Num);
		const int32 EffectiveNum = Num;
		const float Spacing = 1.0f / static_cast<float>(EffectiveNum - 1);
		if (bForceZero && Levels.Num() > 1) Levels[1] = 0.0f;
		for (int32 I = bForceZero ? 2 : 1; I < EffectiveNum; ++I) Levels[I] = Levels[I - 1] + Spacing;

		FRandomStream Random(Seed);
		for (int32 Pass = 0; Pass < 10; ++Pass)
		{
			for (int32 I = 1; I < EffectiveNum - 1; ++I)
			{
				const float Span = Levels[I + 1] - Levels[I - 1];
				const float Candidate = Levels[I - 1] + Span * Random.FRand();
				Levels[I] = Levels[I] * Uniformity + Candidate * (1.0f - Uniformity);
			}
		}

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float Original = FMath::Clamp(Source.AtInterior(X, Y), 0.0f, 1.0f);
				int32 K = 0;
				while (K < EffectiveNum - 2 && Levels[K + 1] <= Original) ++K;
				const float Range = FMath::Max(Levels[K + 1] - Levels[K], UE_SMALL_NUMBER);
				const float T = FMath::Clamp((Original - Levels[K]) / Range, 0.0f, 1.0f);
				float Curve = 3.0f * T * T - 2.0f * T * T * T;
				Curve = (Curve - 0.5f) * 2.0f;
				const bool bNegative = Curve < 0.0f;
				Curve = FMath::Abs(Curve);
				if (bNegative) Curve = -Curve;
				Curve = Curve * 0.5f + 0.5f;
				Curve = FMath::Pow(FMath::Clamp(Curve, 0.0f, 1.0f), 1.0f - FMath::Clamp(Steepness, 0.0f, 1.0f));
				const float Terraced = Levels[K] * (1.0f - Curve) + Levels[K + 1] * Curve;
				OutField.AtInterior(X, Y) = Terraced * Intensity + Original * (1.0f - Intensity);
			}
		}
		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	bool DirectionWarpPixels(const FGaeaScalarField& Source, const FGaeaScalarField& Custom, float StrengthPixels, float DirectionDegrees, EEdgeBehaviour EdgeBehaviour, FGaeaScalarField& OutField, FString* OutError)
	{
		if (!Source.IsValid() || !Custom.IsValid() || Source.Domain != Custom.Domain)
		{
			if (OutError) *OutError = TEXT("Directional warp requires matching valid source/custom fields.");
			return false;
		}
		const float Radians = FMath::DegreesToRadians(DirectionDegrees);
		const FVector2D Direction(-FMath::Cos(Radians) * StrengthPixels, FMath::Sin(Radians) * StrengthPixels);
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const FVector2D Offset = Direction * (Custom.AtInterior(X, Y) - 0.5f);
				OutField.AtInterior(X, Y) = Bilinear(Source, X + Offset.X, Y + Offset.Y, EdgeBehaviour);
			}
		}
		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	bool DirectionWarpNormalized(const FGaeaScalarField& Source, const FGaeaScalarField& Custom, float Strength, float DirectionDegrees, EEdgeBehaviour EdgeBehaviour, FGaeaScalarField& OutField, FString* OutError)
	{
		return DirectionWarpPixels(Source, Custom, Strength * static_cast<float>(Source.Domain.Dimensions.X), DirectionDegrees, EdgeBehaviour, OutField, OutError);
	}

	bool FractalWarp(const FGaeaScalarField& Source, const FFractalWarpSettings& Settings, FGaeaScalarField& OutField, FString* OutError)
	{
		if (!Source.IsValid())
		{
			if (OutError) *OutError = TEXT("Fractal warp requires a valid source field.");
			return false;
		}
		if (Settings.Modulator && Settings.Modulator->Domain != Source.Domain)
		{
			if (OutError) *OutError = TEXT("Fractal warp modulator must share the source domain.");
			return false;
		}
		FGaeaScalarField Current = Source;
		const float MinDim = static_cast<float>(FMath::Min(Source.Domain.Dimensions.X, Source.Domain.Dimensions.Y));
		const float BaseFrequency = 1.0f / FMath::Max(Settings.Size, 0.0001f);
		const float ModDir = FMath::DegreesToRadians(Settings.ModulationDirectionDegrees);
		for (int32 Iter = 0; Iter < FMath::Max(Settings.Iterations, 1); ++Iter)
		{
			FGaeaScalarField Next = Current;
			const float IterStrength = Settings.bPersistStrength ? Settings.Strength : Settings.Strength / static_cast<float>(Iter + 1);
			const float ModeScale = Settings.Mode == TEXT("Bitmap") ? 1.0f : (Settings.Mode == TEXT("Vector Field Integral") ? 1.15f : 0.9f);
			for (int32 Y = 0; Y < Current.Domain.Dimensions.Y; ++Y)
			{
				const float V = Current.Domain.Dimensions.Y > 1 ? static_cast<float>(Y) / static_cast<float>(Current.Domain.Dimensions.Y - 1) - 0.5f : 0.0f;
				for (int32 X = 0; X < Current.Domain.Dimensions.X; ++X)
				{
					const float U = Current.Domain.Dimensions.X > 1 ? static_cast<float>(X) / static_cast<float>(Current.Domain.Dimensions.X - 1) - 0.5f : 0.0f;
					float WX = WarpNoise(U * BaseFrequency, V * BaseFrequency, Settings.NoiseType, Settings.Octaves, Settings.Roughness, Settings.Seed + Iter * 131, 0x211u, Settings.Jitter);
					float WY = WarpNoise(U * BaseFrequency, V * BaseFrequency, Settings.NoiseType, Settings.Octaves, Settings.Roughness, Settings.Seed + Iter * 131, 0x917u, Settings.Jitter);
					if (Settings.Perturbation > UE_SMALL_NUMBER)
					{
						WX += PerlinFractal(U * BaseFrequency * 3.0f, V * BaseFrequency * 3.0f, 3, 0.55f, Settings.Seed + 517, 0x53u) * Settings.Perturbation;
						WY += PerlinFractal(U * BaseFrequency * 3.0f, V * BaseFrequency * 3.0f, 3, 0.55f, Settings.Seed + 911, 0x79u) * Settings.Perturbation;
					}
					if (Settings.bNormalized)
					{
						const float L = FMath::Sqrt(WX * WX + WY * WY);
						if (L > UE_SMALL_NUMBER) { WX /= L; WY /= L; }
					}
					float LocalMod = 1.0f;
					if (Settings.Modulator)
					{
						const float M = FMath::Clamp(Settings.Modulator->AtInterior(X, Y), 0.0f, 1.0f);
						const float Directed = 0.5f + 0.5f * (WX * FMath::Cos(ModDir) + WY * FMath::Sin(ModDir));
						LocalMod = FMath::Lerp(1.0f, M * Directed, Settings.Modulation);
					}
					const float HeightResponse = FMath::Lerp(1.0f, FMath::Abs(Current.AtInterior(X, Y)), FMath::Clamp(Settings.ZScale, 0.0f, 1.0f));
					const float Displacement = IterStrength * MinDim * 0.05f * ModeScale * LocalMod * HeightResponse;
					Next.AtInterior(X, Y) = Bilinear(Current, X - WX * Displacement, Y - WY * Displacement, Settings.EdgeBehaviour);
				}
			}
			Current = MoveTemp(Next);
		}
		OutField = MoveTemp(Current);
		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	void ApplyRadialGradientMultiply(FGaeaScalarField& Field, float CenterX, float CenterY, float RadiusPixels, float Height)
	{
		const float RadiusInv = 1.0f / FMath::Max(RadiusPixels, UE_SMALL_NUMBER);
		for (int32 Y = 0; Y < Field.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Field.Domain.Dimensions.X; ++X)
			{
				const float DX = static_cast<float>(X) - CenterX;
				const float DY = static_cast<float>(Y) - CenterY;
				float A = FMath::Max(1.0f - FMath::Sqrt(DX * DX + DY * DY) * RadiusInv, 0.0f);
				A = A * A * (3.0f - 2.0f * A);
				Field.AtInterior(X, Y) *= A * Height;
			}
		}
	}

	void NormalizePositive(FGaeaScalarField& Field)
	{
		float MaxValue = 0.0f;
		for (const float Value : Field.Values) MaxValue = FMath::Max(MaxValue, Value);
		if (MaxValue <= UE_SMALL_NUMBER) return;
		const float InvMax = 1.0f / MaxValue;
		for (float& Value : Field.Values) Value = FMath::Max(Value, 0.0f) * InvMax;
	}
}
