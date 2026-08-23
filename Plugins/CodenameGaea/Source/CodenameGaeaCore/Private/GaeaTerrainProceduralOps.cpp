#include "GaeaTerrainProceduralOps.h"

#include "GaeaTerrainFieldNames.h"
#include "Math/RandomStream.h"

namespace GaeaTerrainProceduralOps
{
	namespace
	{
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
				Sum += Perlin(X * Frequency, Y * Frequency, Seed + I * 1013, Salt + I * 7919u) * Amplitude;
				Weight += Amplitude;
				Frequency *= 2.0f;
				Amplitude *= Gain;
			}
			return Weight > UE_SMALL_NUMBER ? Sum / Weight : 0.0f;
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

		float VoronoiForm(const FVoronoiSample& S, FName Form)
		{
			const float Peak = FMath::Clamp(1.0f - S.F1, 0.0f, 1.0f);
			const float Neighbor = FMath::Clamp(1.0f - S.F2 * 0.72f, 0.0f, 1.0f);
			const float EdgeDistance = FMath::Max(S.F2 - S.F1, 0.0f);
			const float Ridge = FMath::Clamp(1.0f - EdgeDistance * 2.15f, 0.0f, 1.0f);
			if (Form == TEXT("C")) return S.Cell;
			if (Form == TEXT("N")) return Neighbor;
			if (Form == TEXT("R")) return FMath::Pow(Ridge, 1.45f);
			if (Form == TEXT("S")) return FMath::Pow(Peak, 2.45f) * FMath::Pow(FMath::Clamp(EdgeDistance * 2.0f, 0.0f, 1.0f), 0.42f);
			if (Form == TEXT("M")) return FMath::Clamp(Peak * (0.62f + S.Cell * 0.68f), 0.0f, 1.0f);
			if (Form == TEXT("D")) return FMath::Pow(Ridge, 2.15f);
			if (Form == TEXT("A")) return FMath::Clamp(FMath::Lerp(FMath::Pow(Peak, 1.34f), Peak * (0.62f + S.Cell * 0.68f), 0.5f), 0.0f, 1.0f);
			return FMath::Clamp(FMath::Pow(Peak, 1.34f) * FMath::Lerp(0.72f, 1.20f, S.Cell), 0.0f, 1.0f);
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
			if (Type == TEXT("Voronoi R")) return VoronoiForm(S, TEXT("R")) * 2.0f - 1.0f;
			if (Type == TEXT("Voronoi A")) return VoronoiForm(S, TEXT("A")) * 2.0f - 1.0f;
			if (Type == TEXT("Voronoi S")) return VoronoiForm(S, TEXT("S")) * 2.0f - 1.0f;
			if (Type == TEXT("Voronoi M")) return VoronoiForm(S, TEXT("M")) * 2.0f - 1.0f;
			if (Type == TEXT("Voronoi D")) return VoronoiForm(S, TEXT("D")) * 2.0f - 1.0f;
			return VoronoiForm(S, TEXT("P")) * 2.0f - 1.0f;
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
		const float Scale = FMath::Clamp(Settings.Scale, 0.0001f, 4.0f);
		const float BaseFrequency = 18.0f * Scale;
		const bool bWarp = Settings.WarpType != TEXT("None") && Settings.WarpAmplitude > UE_SMALL_NUMBER;
		for (int32 Y = 0; Y < H; ++Y)
		{
			const float V = H > 1 ? static_cast<float>(Y) / static_cast<float>(H - 1) - 0.5f : 0.0f;
			for (int32 X = 0; X < W; ++X)
			{
				const float U = W > 1 ? static_cast<float>(X) / static_cast<float>(W - 1) - 0.5f : 0.0f;
				float PX = (U + Settings.X) * BaseFrequency * Settings.ScaleX;
				float PY = (V + Settings.Y) * BaseFrequency * Settings.ScaleY;
				if (bWarp)
				{
					const float F = FMath::Max(Settings.WarpFrequency, 0.0001f) * 3.5f;
					const int32 O = FMath::Clamp(Settings.WarpOctaves, 1, 14);
					const float R = Settings.WarpType == TEXT("Complex") ? 0.58f : 0.48f;
					PX += PerlinFractal(U * F, V * F, O, R, Settings.Seed + 1201, 0x31u) * Settings.WarpAmplitude * 2.2f;
					PY += PerlinFractal(U * F, V * F, O, R, Settings.Seed + 2707, 0x57u) * Settings.WarpAmplitude * 2.2f;
				}
				float Value = VoronoiForm(SampleVoronoi(PX, PY, Settings.Jitter, Settings.Function, Settings.Seed), Settings.Form);
				if (!FMath::IsNearlyEqual(Settings.Gain, 0.5f))
				{
					const float Gamma = FMath::Lerp(2.0f, 0.5f, FMath::Clamp(Settings.Gain, 0.0f, 1.0f));
					Value = FMath::Pow(FMath::Clamp(Value, 0.0f, 1.0f), Gamma);
				}
				OutField.AtInterior(X, Y) = FMath::Clamp(Value, 0.0f, 1.0f);
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
		const float Frequency = 1.0f / FMath::Max(Settings.Scale, 0.0001f);
		for (int32 Y = 0; Y < H; ++Y)
		{
			const float V = H > 1 ? static_cast<float>(Y) / static_cast<float>(H - 1) - 0.5f : 0.0f;
			for (int32 X = 0; X < W; ++X)
			{
				const float U = W > 1 ? static_cast<float>(X) / static_cast<float>(W - 1) - 0.5f : 0.0f;
				float PX = (U + Settings.X) * Frequency / FMath::Max(Settings.ScaleX, 0.0001f);
				float PY = (V + Settings.Y) * Frequency / FMath::Max(Settings.ScaleY, 0.0001f);
				if (Settings.WarpType != TEXT("None") && Settings.WarpAmplitude > UE_SMALL_NUMBER)
				{
					const float WF = FMath::Max(Settings.WarpFrequency, 0.0001f) * Frequency;
					const int32 WO = FMath::Clamp(Settings.WarpOctaves, 1, 14);
					const float WR = Settings.WarpType == TEXT("Complex") ? 0.58f : 0.48f;
					PX += PerlinFractal(U * WF, V * WF, WO, WR, Settings.Seed + 1301, 0x51u) * Settings.WarpAmplitude;
					PY += PerlinFractal(U * WF, V * WF, WO, WR, Settings.Seed + 2903, 0x79u) * Settings.WarpAmplitude;
				}
				float Value = PerlinFractal(PX, PY, FMath::Clamp(Settings.Octaves, 1, 14), FMath::Clamp(Settings.Gain, 0.0f, 1.0f), Settings.Seed, 0x151u);
				if (Settings.Type == TEXT("Ridged")) Value = 1.0f - FMath::Abs(Value);
				else if (Settings.Type == TEXT("Billowy")) Value = FMath::Abs(Value) * 2.0f - 1.0f;
				OutField.AtInterior(X, Y) = FMath::Clamp(Value * 0.5f + 0.5f, 0.0f, 1.0f);
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
