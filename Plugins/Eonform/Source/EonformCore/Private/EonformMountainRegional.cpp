#include "EonformMountainRegional.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainFractalWarp.h"
#include "EonformTerrainGlobalSummary.h"
#include "EonformTerrainProceduralOps.h"

namespace
{
	FVector2D WorldToReferenceCoordinate(const FVector2d& World, const FEonformGridDomain& ReferenceDomain)
	{
		const FVector2d Size = ReferenceDomain.WorldSize();
		return FVector2D(
			static_cast<float>((World.X - ReferenceDomain.WorldMin.X) / Size.X * static_cast<double>(ReferenceDomain.Dimensions.X - 1)),
			static_cast<float>((World.Y - ReferenceDomain.WorldMin.Y) / Size.Y * static_cast<double>(ReferenceDomain.Dimensions.Y - 1)));
	}

	EonformTerrainProceduralOps::FFractalWarpSettings MakeMountainPreWarpSettings(int32 Seed)
	{
		EonformTerrainProceduralOps::FFractalWarpSettings Settings;
		Settings.Size = 0.5f;
		Settings.Strength = 0.5f;
		Settings.bPersistStrength = true;
		Settings.ZScale = 0.0f;
		Settings.NoiseType = TEXT("Perlin FBM");
		Settings.Perturbation = 0.5f;
		Settings.Octaves = 12;
		Settings.Roughness = 0.5f;
		Settings.bNormalized = false;
		Settings.Iterations = 1;
		Settings.Mode = TEXT("Vector Field");
		Settings.EdgeBehaviour = EonformTerrainProceduralOps::EEdgeBehaviour::Edge;
		Settings.Seed = Seed;
		Settings.Modulation = 0.0f;
		Settings.ModulationDirectionDegrees = 45.0f;
		Settings.Jitter = 0.45f;
		return Settings;
	}

	uint64 MountainRidgeSummaryKey(const FGuid& NodeId, uint32 Layer)
	{
		const uint64 High = (static_cast<uint64>(NodeId.A) << 32) | static_cast<uint64>(NodeId.B);
		const uint64 Low = (static_cast<uint64>(NodeId.C) << 32) | static_cast<uint64>(NodeId.D);
		uint64 Key = High ^ ((Low << 17) | (Low >> 47));
		Key ^= 0x4D4F554E5441494Eull;
		Key ^= (static_cast<uint64>(Layer) + 1ull) * 0x9E3779B97F4A7C15ull;
		return Key;
	}

	uint64 MountainCoreRangeSummaryKey(const FGuid& NodeId, bool bMaximum)
	{
		const uint64 High = (static_cast<uint64>(NodeId.A) << 32) | static_cast<uint64>(NodeId.B);
		const uint64 Low = (static_cast<uint64>(NodeId.C) << 32) | static_cast<uint64>(NodeId.D);
		uint64 Key = High ^ ((Low << 17) | (Low >> 47));
		Key ^= 0x4D4F554E52414E47ull;
		Key ^= bMaximum ? 0xA11CE5A1F00D0001ull : 0xA11CE5A1F00D0002ull;
		return Key;
	}

	float RadialMultiplier(const FVector2D& Coordinate, const FIntPoint& Dimensions, float MountainScale, float XCenter, float YCenter)
	{
		const float CenterX = static_cast<float>(Dimensions.X) * XCenter;
		const float CenterY = static_cast<float>(Dimensions.Y) * YCenter;
		const float Radius = FMath::Max(static_cast<float>(Dimensions.X) * MountainScale, UE_SMALL_NUMBER);
		const float DX = Coordinate.X - CenterX;
		const float DY = Coordinate.Y - CenterY;
		float A = FMath::Max(1.0f - FMath::Sqrt(DX * DX + DY * DY) / Radius, 0.0f);
		return A * A * (3.0f - 2.0f * A);
	}

	void AddBilinearDependencies(
		const FVector2D& Coordinate,
		const FIntPoint& Dimensions,
		TSet<FIntPoint>& Coordinates)
	{
		const float X = FMath::Clamp(Coordinate.X, 0.0f, static_cast<float>(Dimensions.X - 1));
		const float Y = FMath::Clamp(Coordinate.Y, 0.0f, static_cast<float>(Dimensions.Y - 1));
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const int32 X1 = FMath::Min(X0 + 1, Dimensions.X - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, Dimensions.Y - 1);
		Coordinates.Add(FIntPoint(X0, Y0));
		Coordinates.Add(FIntPoint(X1, Y0));
		Coordinates.Add(FIntPoint(X0, Y1));
		Coordinates.Add(FIntPoint(X1, Y1));
	}
}

bool EonformMountainRegional::GenerateCore(
	const FEonformGridDomain& TargetDomain,
	const FEonformGridDomain& ReferenceDomain,
	const FEonformRidgeSettings& RidgeSettings0,
	const FEonformRidgeSettings& RidgeSettings1,
	const FEonformRidgeSettings& RidgeSettings2,
	float MountainScale,
	float XCenter,
	float YCenter,
	int32 WarpSeed,
	bool bApplyPreWarp,
	const FGuid& MountainNodeId,
	const TSharedPtr<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe>& SummaryCache,
	FEonformScalarField& OutHeight,
	FString* OutError)
{
	if (!TargetDomain.IsValid() || !ReferenceDomain.IsValid())
	{
		if (OutError) *OutError = TEXT("Regional Mountain requires valid target and reference domains.");
		return false;
	}

	const EonformTerrainProceduralOps::FFractalWarpSettings WarpSettings = MakeMountainPreWarpSettings(WarpSeed);
	const FIntPoint Storage = TargetDomain.GetStorageDimensions();
	TArray<FVector2D> ReferenceCoordinates;
	TArray<FVector2D> WarpedCoordinates;
	ReferenceCoordinates.SetNumUninitialized(Storage.X * Storage.Y);
	if (bApplyPreWarp) WarpedCoordinates.SetNumUninitialized(Storage.X * Storage.Y);

	TSet<FIntPoint> DependencySet;
	DependencySet.Reserve(Storage.X * Storage.Y * (bApplyPreWarp ? 8 : 4));
	for (int32 Y = 0; Y < Storage.Y; ++Y)
	{
		for (int32 X = 0; X < Storage.X; ++X)
		{
			const int32 Index = Y * Storage.X + X;
			const FVector2D ReferenceCoordinate = WorldToReferenceCoordinate(TargetDomain.StorageSampleToWorld(X, Y), ReferenceDomain);
			ReferenceCoordinates[Index] = ReferenceCoordinate;
			AddBilinearDependencies(ReferenceCoordinate, ReferenceDomain.Dimensions, DependencySet);

			if (bApplyPreWarp)
			{
				FVector2D WarpedCoordinate;
				if (!EonformTerrainProceduralOps::FractalWarpVectorCoordinate(
					ReferenceCoordinate,
					ReferenceDomain.Dimensions,
					WarpSettings,
					WarpedCoordinate,
					OutError)) return false;
				WarpedCoordinates[Index] = WarpedCoordinate;
				AddBilinearDependencies(WarpedCoordinate, ReferenceDomain.Dimensions, DependencySet);
			}
		}
	}

	TArray<FIntPoint> Dependencies = DependencySet.Array();
	TArray<float> Ridge0;
	TArray<float> Ridge1;
	TArray<float> Ridge2;
	if (!FEonformRidgeGenerator::SampleRegionalReference(
		ReferenceDomain,
		RidgeSettings0,
		Dependencies,
		SummaryCache,
		MountainRidgeSummaryKey(MountainNodeId, 0),
		Ridge0,
		OutError)
		|| !FEonformRidgeGenerator::SampleRegionalReference(
			ReferenceDomain,
			RidgeSettings1,
			Dependencies,
			SummaryCache,
			MountainRidgeSummaryKey(MountainNodeId, 1),
			Ridge1,
			OutError)
		|| !FEonformRidgeGenerator::SampleRegionalReference(
			ReferenceDomain,
			RidgeSettings2,
			Dependencies,
			SummaryCache,
			MountainRidgeSummaryKey(MountainNodeId, 2),
			Ridge2,
			OutError))
	{
		return false;
	}

	TMap<FIntPoint, float> MountainSamples;
	MountainSamples.Reserve(Dependencies.Num());
	for (int32 I = 0; I < Dependencies.Num(); ++I)
	{
		const FVector2D Coordinate(static_cast<float>(Dependencies[I].X), static_cast<float>(Dependencies[I].Y));
		const float RidgeSum = Ridge0[I] + Ridge1[I] + Ridge2[I];
		MountainSamples.Add(
			Dependencies[I],
			RidgeSum * RadialMultiplier(Coordinate, ReferenceDomain.Dimensions, MountainScale, XCenter, YCenter));
	}

	auto SampleMountain = [&MountainSamples](int32 X, int32 Y)
	{
		if (const float* Value = MountainSamples.Find(FIntPoint(X, Y))) return *Value;
		checkNoEntry();
		return 0.0f;
	};

	FEonformFieldDescriptor Descriptor;
	Descriptor.Name = EonformTerrainFieldNames::Height;
	Descriptor.Unit = EEonformFieldUnit::Normalized;
	Descriptor.Interpolation = EEonformInterpolation::Bilinear;
	OutHeight.Initialize(TargetDomain, Descriptor, 0.0f);

	for (int32 Y = 0; Y < Storage.Y; ++Y)
	{
		for (int32 X = 0; X < Storage.X; ++X)
		{
			const int32 Index = Y * Storage.X + X;
			const FVector2D ReferenceCoordinate = ReferenceCoordinates[Index];
			const float Original = EonformTerrainProceduralOps::FractalWarpSampleBilinear(
				SampleMountain,
				ReferenceDomain.Dimensions,
				ReferenceCoordinate.X,
				ReferenceCoordinate.Y,
				EonformTerrainProceduralOps::EEdgeBehaviour::Edge);
			float Value = Original;
			if (bApplyPreWarp)
			{
				const FVector2D WarpedCoordinate = WarpedCoordinates[Index];
				const float Warped = EonformTerrainProceduralOps::FractalWarpSampleBilinear(
					SampleMountain,
					ReferenceDomain.Dimensions,
					WarpedCoordinate.X,
					WarpedCoordinate.Y,
					WarpSettings.EdgeBehaviour);
				Value = FMath::Min(Original, Warped);
			}
			OutHeight.AtStorage(X, Y) = Value;
		}
	}

	if (OutError) OutError->Reset();
	return OutHeight.IsValid();
}

bool EonformMountainRegional::ResolveCoreRange(
	const FEonformGridDomain& ReferenceDomain,
	const FEonformRidgeSettings& RidgeSettings0,
	const FEonformRidgeSettings& RidgeSettings1,
	const FEonformRidgeSettings& RidgeSettings2,
	float MountainScale,
	float XCenter,
	float YCenter,
	int32 WarpSeed,
	bool bApplyPreWarp,
	const FGuid& MountainNodeId,
	const TSharedPtr<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe>& SummaryCache,
	float& OutMinimum,
	float& OutMaximum,
	FString* OutError)
{
	if (!ReferenceDomain.IsValid() || !SummaryCache.IsValid())
	{
		if (OutError) *OutError = TEXT("Mountain core range requires a valid reference domain and shared summary cache.");
		return false;
	}

	const uint64 MinimumKey = MountainCoreRangeSummaryKey(MountainNodeId, false);
	const uint64 MaximumKey = MountainCoreRangeSummaryKey(MountainNodeId, true);
	float CachedMinimum = 0.0f;
	float CachedMaximum = 0.0f;
	if (SummaryCache->Find(MinimumKey, CachedMinimum) && SummaryCache->Find(MaximumKey, CachedMaximum))
	{
		OutMinimum = CachedMinimum;
		OutMaximum = CachedMaximum;
		if (OutError) OutError->Reset();
		return true;
	}

	float Minimum = TNumericLimits<float>::Max();
	float Maximum = TNumericLimits<float>::Lowest();
	int32 StartY = 0;
	while (StartY < ReferenceDomain.Dimensions.Y)
	{
		int32 Remaining = ReferenceDomain.Dimensions.Y - StartY;
		if (Remaining == 1 && StartY > 0)
		{
			--StartY;
			Remaining = 2;
		}
		const int32 RowCount = FMath::Min(FEonformTerrainGlobalSummary::PreferredStripRows, Remaining);
		const int32 EndY = StartY + RowCount - 1;

		const FVector2d WorldMin = ReferenceDomain.InteriorSampleToWorld(0, StartY);
		const FVector2d WorldMax = ReferenceDomain.InteriorSampleToWorld(ReferenceDomain.Dimensions.X - 1, EndY);
		const FEonformGridDomain StripDomain = FEonformGridDomain::Make(
			FIntPoint(ReferenceDomain.Dimensions.X, RowCount),
			WorldMin,
			WorldMax);

		FEonformScalarField Strip;
		if (!GenerateCore(
			StripDomain,
			ReferenceDomain,
			RidgeSettings0,
			RidgeSettings1,
			RidgeSettings2,
			MountainScale,
			XCenter,
			YCenter,
			WarpSeed,
			bApplyPreWarp,
			MountainNodeId,
			SummaryCache,
			Strip,
			OutError))
		{
			return false;
		}

		for (int32 Y = 0; Y < Strip.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Strip.Domain.Dimensions.X; ++X)
			{
				const float Value = Strip.AtInterior(X, Y);
				Minimum = FMath::Min(Minimum, Value);
				Maximum = FMath::Max(Maximum, Value);
			}
		}

		if (EndY >= ReferenceDomain.Dimensions.Y - 1) break;
		StartY = EndY + 1;
	}

	if (!FMath::IsFinite(Minimum) || !FMath::IsFinite(Maximum) || Maximum < Minimum)
	{
		if (OutError) *OutError = TEXT("Mountain core range reduction produced invalid extrema.");
		return false;
	}

	SummaryCache->Store(MinimumKey, Minimum);
	SummaryCache->Store(MaximumKey, Maximum);
	OutMinimum = Minimum;
	OutMaximum = Maximum;
	if (OutError) OutError->Reset();
	return true;
}
