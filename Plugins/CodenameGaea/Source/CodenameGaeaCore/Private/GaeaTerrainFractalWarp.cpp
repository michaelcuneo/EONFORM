#include "GaeaTerrainFractalWarp.h"

namespace GaeaTerrainProceduralOps
{
	namespace
	{
		uint32 WarpHash(uint32 X)
		{
			X ^= X >> 16;
			X *= 0x7feb352dU;
			X ^= X >> 15;
			X *= 0x846ca68bU;
			X ^= X >> 16;
			return X;
		}

		float WarpHash01(int32 X, int32 Y, int32 Seed, uint32 Salt)
		{
			uint32 H = static_cast<uint32>(X) * 0x9e3779b9U;
			H ^= static_cast<uint32>(Y) * 0x85ebca6bU;
			H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U;
			H ^= Salt;
			return static_cast<float>(WarpHash(H) & 0x00ffffffU) / 16777215.0f;
		}

		float Fade(float T)
		{
			return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
		}

		float Perlin(float X, float Y, int32 Seed, uint32 Salt)
		{
			const int32 X0 = FMath::FloorToInt(X);
			const int32 Y0 = FMath::FloorToInt(Y);
			const float TX = X - static_cast<float>(X0);
			const float TY = Y - static_cast<float>(Y0);
			const float SX = Fade(TX);
			const float SY = Fade(TY);

			auto Dot = [&](int32 GX, int32 GY, float DX, float DY)
			{
				const float A = WarpHash01(GX, GY, Seed, Salt) * 2.0f * PI;
				return FMath::Cos(A) * DX + FMath::Sin(A) * DY;
			};

			const float A = FMath::Lerp(
				Dot(X0, Y0, TX, TY),
				Dot(X0 + 1, Y0, TX - 1.0f, TY),
				SX);
			const float B = FMath::Lerp(
				Dot(X0, Y0 + 1, TX, TY - 1.0f),
				Dot(X0 + 1, Y0 + 1, TX - 1.0f, TY - 1.0f),
				SX);
			return FMath::Clamp(FMath::Lerp(A, B, SY) * 1.41421356f, -1.0f, 1.0f);
		}

		float Fbm(float X, float Y, int32 Octaves, float Gain, int32 Seed, uint32 Salt)
		{
			float Result = 0.0f;
			float Amplitude = 1.0f;
			float Weight = 0.0f;
			float Frequency = 1.0f;
			for (int32 I = 0; I < FMath::Max(Octaves, 1); ++I)
			{
				Result += Perlin(X * Frequency, Y * Frequency, Seed + I, Salt) * Amplitude;
				Weight += Amplitude;
				Frequency *= 2.0f;
				Amplitude *= Gain;
			}
			return Weight > UE_SMALL_NUMBER ? Result / Weight : 0.0f;
		}

		float MirrorCoord(float V, int32 MaxIndex)
		{
			if (MaxIndex <= 0) return 0.0f;
			const float Period = static_cast<float>(MaxIndex * 2);
			float R = FMath::Fmod(V, Period);
			if (R < 0.0f) R += Period;
			return R > MaxIndex ? Period - R : R;
		}

		float Sample(const FGaeaScalarField& Field, float X, float Y, EEdgeBehaviour Edge)
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
			const float TX = X - X0;
			const float TY = Y - Y0;
			return FMath::Lerp(
				FMath::Lerp(Field.AtInterior(X0, Y0), Field.AtInterior(X1, Y0), TX),
				FMath::Lerp(Field.AtInterior(X0, Y1), Field.AtInterior(X1, Y1), TX),
				TY);
		}

		void SampleWarpVector(
			float X,
			float Y,
			float Frequency,
			const FFractalWarpSettings& Settings,
			int32 Seed,
			float& OutX,
			float& OutY)
		{
			// PerlinFBM is the most important path for Mountain/Ridge. Keep the
			// observed frequency/roughness/octave semantics exact at this level.
			OutX = Fbm(X * Frequency, Y * Frequency, Settings.Octaves, Settings.Roughness, Seed, 0x211u);
			OutY = Fbm(X * Frequency, Y * Frequency, Settings.Octaves, Settings.Roughness, Seed + 1, 0x917u);

			if (Settings.bNormalized)
			{
				const float LenSq = OutX * OutX + OutY * OutY + 0.1f;
				const float InvLen = 0.5f / FMath::Sqrt(LenSq);
				OutX *= InvLen;
				OutY *= InvLen;
			}
		}
	}

	bool FractalWarpFidelity(
		const FGaeaScalarField& Source,
		const FFractalWarpSettings& Settings,
		FGaeaScalarField& OutField,
		FString* OutError)
	{
		if (!Source.IsValid())
		{
			if (OutError) *OutError = TEXT("FractalWarp requires a valid source field.");
			return false;
		}
		if (Settings.Modulator && Settings.Modulator->Domain != Source.Domain)
		{
			if (OutError) *OutError = TEXT("FractalWarp modulator must share the source domain.");
			return false;
		}

		const int32 W = Source.Domain.Dimensions.X;
		const int32 H = Source.Domain.Dimensions.Y;
		const float Resolution = static_cast<float>(W);
		const float Size = FMath::Max(Settings.Size, 0.0001f);
		const float Frequency = 1.0f / (Size * FMath::Max(Resolution, 1.0f));
		const float Warp = Settings.Strength * Resolution * (Settings.bPersistStrength ? Size : 1.0f);
		const int32 Iterations = FMath::Max(Settings.Iterations, 1);
		const float DirectionRadians = FMath::DegreesToRadians(Settings.ModulationDirectionDegrees);
		const FVector2D ModAxis(-FMath::Cos(DirectionRadians), FMath::Sin(DirectionRadians));

		// Virtual mode composes a coordinate field, then samples the original map
		// once. This is the mode used by default by the supplied FractalWarp.
		if (Settings.Mode != TEXT("Bitmap") && Settings.Mode != TEXT("Vector Field Integral"))
		{
			TArray<FVector2D> Coordinates;
			Coordinates.SetNumUninitialized(W * H);
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					Coordinates[Y * W + X] = FVector2D(X, Y);
				}
			}

			for (int32 Iter = 0; Iter < Iterations; ++Iter)
			{
				TArray<FVector2D> Next = Coordinates;
				for (int32 Y = 0; Y < H; ++Y)
				{
					for (int32 X = 0; X < W; ++X)
					{
						const FVector2D P = Coordinates[Y * W + X];
						float WX = 0.0f;
						float WY = 0.0f;
						SampleWarpVector(P.X, P.Y, Frequency, Settings, Settings.Seed + Iter * 2, WX, WY);

						float ModOffset = 0.0f;
						if (Settings.Modulator && Settings.Modulation > 0.0f)
						{
							ModOffset = (Settings.Modulator->AtInterior(X, Y) - 0.5f)
								* Settings.Modulation
								* Resolution;
						}

						Next[Y * W + X] = FVector2D(
							P.X + WX * Warp + ModAxis.X * ModOffset,
							P.Y + WY * Warp + ModAxis.Y * ModOffset);
					}
				}
				Coordinates = MoveTemp(Next);
			}

			OutField = Source;
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					const FVector2D P = Coordinates[Y * W + X];
					OutField.AtInterior(X, Y) = Sample(Source, P.X, P.Y, Settings.EdgeBehaviour);
				}
			}
		}
		else
		{
			// Real/Integral are applied incrementally. This keeps the same displacement
			// equation and avoids the old arbitrary 0.05 multiplier.
			FGaeaScalarField Current = Source;
			for (int32 Iter = 0; Iter < Iterations; ++Iter)
			{
				FGaeaScalarField Next = Current;
				for (int32 Y = 0; Y < H; ++Y)
				{
					for (int32 X = 0; X < W; ++X)
					{
						float WX = 0.0f;
						float WY = 0.0f;
						SampleWarpVector(static_cast<float>(X), static_cast<float>(Y), Frequency, Settings, Settings.Seed + Iter * 2, WX, WY);
						Next.AtInterior(X, Y) = Sample(
							Current,
							X + WX * Warp,
							Y + WY * Warp,
							Settings.EdgeBehaviour);
					}
				}
				Current = MoveTemp(Next);
			}
			OutField = MoveTemp(Current);
		}

		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}
}
