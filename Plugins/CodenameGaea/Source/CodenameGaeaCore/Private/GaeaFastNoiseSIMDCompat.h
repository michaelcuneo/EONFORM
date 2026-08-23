#pragma once

#include "CoreMinimal.h"

// Scalar compatibility primitives for the FastNoiseSIMD algorithms used by
// Gaea RawNoise. The implementation follows Auburn/FastNoiseSIMD (MIT), but is
// kept scalar so terrain evaluation is deterministic regardless of host SIMD
// level. Coordinates, lattice primes, hash construction, 3D gradient basis and
// cellular feature vectors intentionally match FastNoiseSIMD rather than
// EONFORM's earlier angle/radius approximations.
namespace GaeaFastNoiseSIMDCompat
{
	constexpr int32 XPrime = 1619;
	constexpr int32 YPrime = 31337;
	constexpr int32 ZPrime = 6971;
	constexpr uint32 HashMultiplier = 60493u;
	constexpr uint32 Bit10Mask = 1023u;
	constexpr float CellVectorMidpoint = 511.5f;
	constexpr float HashToFloat = 1.0f / 2147483648.0f;

	enum class ECellularDistance : uint8
	{
		Euclidean,
		Manhattan,
		Natural
	};

	struct FCellularSample
	{
		float F1 = TNumericLimits<float>::Max();
		float F2 = TNumericLimits<float>::Max();
		float CellValue = 0.0f;
		FVector3f Feature = FVector3f::ZeroVector;
	};

	inline int32 WrapAdd(int32 A, int32 B)
	{
		return static_cast<int32>(static_cast<uint32>(A) + static_cast<uint32>(B));
	}

	inline int32 WrapMul(int32 A, int32 B)
	{
		return static_cast<int32>(static_cast<uint32>(A) * static_cast<uint32>(B));
	}

	inline int32 PrimeCoordinate(int32 Coordinate, int32 Prime)
	{
		return WrapMul(Coordinate, Prime);
	}

	inline int32 HashHB(int32 Seed, int32 X, int32 Y, int32 Z)
	{
		uint32 H = static_cast<uint32>(Seed);
		H ^= static_cast<uint32>(X);
		H ^= static_cast<uint32>(Y);
		H ^= static_cast<uint32>(Z);
		H = H * H * HashMultiplier * H;
		return static_cast<int32>(H);
	}

	inline int32 Hash(int32 Seed, int32 X, int32 Y, int32 Z)
	{
		const int32 Cubic = HashHB(Seed, X, Y, Z);
		// FastNoiseSIMD performs an arithmetic 13-bit right shift on the signed
		// hash lane before XORing it back into the hash.
		return Cubic ^ (Cubic >> 13);
	}

	inline float ValueCoordinate(int32 Seed, int32 X, int32 Y, int32 Z)
	{
		return static_cast<float>(HashHB(Seed, X, Y, Z)) * HashToFloat;
	}

	inline float Quintic(float T)
	{
		return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
	}

	inline float GradientCoordinate(int32 Seed, int32 XI, int32 YI, int32 ZI, float X, float Y, float Z)
	{
		const int32 H = Hash(Seed, XI, YI, ZI);
		const int32 H13 = H & 13;
		const float U = H13 < 8 ? X : Y;
		const float V = H13 < 2 ? Y : (H13 == 12 ? X : Z);
		const float SignedU = H < 0 ? -U : U;
		const float SignedV = (H & 2) != 0 ? -V : V;
		return SignedU + SignedV;
	}

	inline float Perlin(float X, float Y, float Z, int32 Seed)
	{
		const float FloorX = FMath::FloorToFloat(X);
		const float FloorY = FMath::FloorToFloat(Y);
		const float FloorZ = FMath::FloorToFloat(Z);

		const int32 X0 = PrimeCoordinate(static_cast<int32>(FloorX), XPrime);
		const int32 Y0 = PrimeCoordinate(static_cast<int32>(FloorY), YPrime);
		const int32 Z0 = PrimeCoordinate(static_cast<int32>(FloorZ), ZPrime);
		const int32 X1 = WrapAdd(X0, XPrime);
		const int32 Y1 = WrapAdd(Y0, YPrime);
		const int32 Z1 = WrapAdd(Z0, ZPrime);

		const float XF0 = X - FloorX;
		const float YF0 = Y - FloorY;
		const float ZF0 = Z - FloorZ;
		const float XF1 = XF0 - 1.0f;
		const float YF1 = YF0 - 1.0f;
		const float ZF1 = ZF0 - 1.0f;
		const float XS = Quintic(XF0);
		const float YS = Quintic(YF0);
		const float ZS = Quintic(ZF0);

		const float Z0Blend = FMath::Lerp(
			FMath::Lerp(
				GradientCoordinate(Seed, X0, Y0, Z0, XF0, YF0, ZF0),
				GradientCoordinate(Seed, X1, Y0, Z0, XF1, YF0, ZF0), XS),
			FMath::Lerp(
				GradientCoordinate(Seed, X0, Y1, Z0, XF0, YF1, ZF0),
				GradientCoordinate(Seed, X1, Y1, Z0, XF1, YF1, ZF0), XS), YS);
		const float Z1Blend = FMath::Lerp(
			FMath::Lerp(
				GradientCoordinate(Seed, X0, Y0, Z1, XF0, YF0, ZF1),
				GradientCoordinate(Seed, X1, Y0, Z1, XF1, YF0, ZF1), XS),
			FMath::Lerp(
				GradientCoordinate(Seed, X0, Y1, Z1, XF0, YF1, ZF1),
				GradientCoordinate(Seed, X1, Y1, Z1, XF1, YF1, ZF1), XS), YS);
		return FMath::Lerp(Z0Blend, Z1Blend, ZS);
	}

	inline float FractalBounding(int32 Octaves, float Gain)
	{
		float Amp = Gain;
		float AmpFractal = 1.0f;
		for (int32 I = 1; I < FMath::Max(Octaves, 1); ++I)
		{
			AmpFractal += Amp;
			Amp *= Gain;
		}
		return 1.0f / AmpFractal;
	}

	inline float PerlinFBM(float X, float Y, float Z, int32 Octaves, float Lacunarity, float Gain, int32 Seed)
	{
		const int32 Count = FMath::Max(Octaves, 1);
		float Result = Perlin(X, Y, Z, Seed);
		float Amp = 1.0f;
		for (int32 I = 1; I < Count; ++I)
		{
			X *= Lacunarity;
			Y *= Lacunarity;
			Z *= Lacunarity;
			Seed = WrapAdd(Seed, 1);
			Amp *= Gain;
			Result += Perlin(X, Y, Z, Seed) * Amp;
		}
		return Result * FractalBounding(Count, Gain);
	}

	inline float PerlinBillow(float X, float Y, float Z, int32 Octaves, float Lacunarity, float Gain, int32 Seed)
	{
		const int32 Count = FMath::Max(Octaves, 1);
		float Result = FMath::Abs(Perlin(X, Y, Z, Seed)) * 2.0f - 1.0f;
		float Amp = 1.0f;
		for (int32 I = 1; I < Count; ++I)
		{
			X *= Lacunarity;
			Y *= Lacunarity;
			Z *= Lacunarity;
			Seed = WrapAdd(Seed, 1);
			Amp *= Gain;
			Result += (FMath::Abs(Perlin(X, Y, Z, Seed)) * 2.0f - 1.0f) * Amp;
		}
		return Result * FractalBounding(Count, Gain);
	}

	inline float PerlinRigidMulti(float X, float Y, float Z, int32 Octaves, float Lacunarity, float Gain, int32 Seed)
	{
		const int32 Count = FMath::Max(Octaves, 1);
		float Result = 1.0f - FMath::Abs(Perlin(X, Y, Z, Seed));
		float Amp = 1.0f;
		for (int32 I = 1; I < Count; ++I)
		{
			X *= Lacunarity;
			Y *= Lacunarity;
			Z *= Lacunarity;
			Seed = WrapAdd(Seed, 1);
			Amp *= Gain;
			Result -= (1.0f - FMath::Abs(Perlin(X, Y, Z, Seed))) * Amp;
		}
		return Result;
	}

	inline float CellularDistance(float DX, float DY, float DZ, ECellularDistance Function, bool bDistanceReturn)
	{
		const float Euclidean = DX * DX + DY * DY + DZ * DZ;
		const float Manhattan = FMath::Abs(DX) + FMath::Abs(DY) + FMath::Abs(DZ);
		if (Function == ECellularDistance::Manhattan) return Manhattan;
		if (Function == ECellularDistance::Natural)
		{
			// FastNoiseSIMD redefines Natural specifically for Distance/Distance2
			// returns. CellValue and NoiseLookup retain Euclidean + Manhattan.
			return bDistanceReturn ? Euclidean * Manhattan : Euclidean + Manhattan;
		}
		return Euclidean;
	}

	inline FCellularSample Cellular(float X, float Y, float Z, float Jitter, ECellularDistance Function, int32 Seed, bool bDistanceReturn = true)
	{
		FCellularSample Result;
		const int32 XBase = FMath::RoundToInt(X) - 1;
		const int32 YBase = FMath::RoundToInt(Y) - 1;
		const int32 ZBase = FMath::RoundToInt(Z) - 1;

		for (int32 XI = 0; XI < 3; ++XI)
		{
			const int32 CellX = XBase + XI;
			const int32 PrimeX = PrimeCoordinate(CellX, XPrime);
			for (int32 YI = 0; YI < 3; ++YI)
			{
				const int32 CellY = YBase + YI;
				const int32 PrimeY = PrimeCoordinate(CellY, YPrime);
				for (int32 ZI = 0; ZI < 3; ++ZI)
				{
					const int32 CellZ = ZBase + ZI;
					const int32 PrimeZ = PrimeCoordinate(CellZ, ZPrime);
					const int32 H = HashHB(Seed, PrimeX, PrimeY, PrimeZ);
					const uint32 Bits = static_cast<uint32>(H);
					float VX = static_cast<float>(Bits & Bit10Mask) - CellVectorMidpoint;
					float VY = static_cast<float>((Bits >> 10) & Bit10Mask) - CellVectorMidpoint;
					float VZ = static_cast<float>((Bits >> 20) & Bit10Mask) - CellVectorMidpoint;
					const float MagnitudeSquared = VX * VX + VY * VY + VZ * VZ;
					const float InvMagnitude = MagnitudeSquared > UE_SMALL_NUMBER ? Jitter / FMath::Sqrt(MagnitudeSquared) : 0.0f;
					VX *= InvMagnitude;
					VY *= InvMagnitude;
					VZ *= InvMagnitude;

					const FVector3f Feature(
						static_cast<float>(CellX) + VX,
						static_cast<float>(CellY) + VY,
						static_cast<float>(CellZ) + VZ);
					const float Distance = CellularDistance(Feature.X - X, Feature.Y - Y, Feature.Z - Z, Function, bDistanceReturn);

					if (Distance < Result.F1)
					{
						Result.F2 = Result.F1;
						Result.F1 = Distance;
						Result.CellValue = static_cast<float>(H) * HashToFloat;
						Result.Feature = Feature;
					}
					else if (Distance < Result.F2)
					{
						Result.F2 = Distance;
					}
				}
			}
		}
		return Result;
	}
}
