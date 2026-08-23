#include "GaeaTerrainLandformOps.h"

#include "GaeaGridDomain.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainFieldNames.h"

namespace
{
	struct FMountainSite
	{
		FVector2D Position = FVector2D::ZeroVector;
		float Weight = 1.0f;
	};

	struct FMountainSpur
	{
		float Angle = 0.0f;
		float Length = 0.5f;
		float Width = 0.05f;
		float Strength = 1.0f;
	};

	FGaeaScalarField MakeField(const FGaeaGridDomain& Domain, FName Name)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	float Smooth01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float DistanceToSegment(const FVector2D& Point, const FVector2D& Start, const FVector2D& End, float& OutT)
	{
		const FVector2D Segment = End - Start;
		const float LengthSquared = Segment.SizeSquared();
		if (LengthSquared <= UE_SMALL_NUMBER)
		{
			OutT = 0.0f;
			return FVector2D::Distance(Point, Start);
		}

		OutT = FMath::Clamp(FVector2D::DotProduct(Point - Start, Segment) / LengthSquared, 0.0f, 1.0f);
		return FVector2D::Distance(Point, Start + Segment * OutT);
	}

	float BulkBase(FName Bulk)
	{
		if (Bulk == TEXT("Low")) return 0.12f;
		if (Bulk == TEXT("High")) return 0.34f;
		return 0.23f;
	}

	float BulkRadiusMultiplier(FName Bulk)
	{
		if (Bulk == TEXT("Low")) return 0.84f;
		if (Bulk == TEXT("High")) return 1.12f;
		return 1.0f;
	}
}

bool FGaeaTerrainLandformOps::BuildMountain(
	const FGaeaMountainLandformSettings& InSettings,
	const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
	FGaeaMountainLandformResult& OutResult,
	FString* OutError)
{
	FGaeaMountainLandformSettings Settings = InSettings;
	Settings.Resolution = FMath::Clamp(Settings.Resolution, 17, 4097);
	Settings.WorldSize = FMath::Max(Settings.WorldSize, 1.0f);
	Settings.HeightScale = FMath::Max(Settings.HeightScale, 1.0f);
	Settings.Scale = FMath::Clamp(Settings.Scale, 0.1f, 2.0f);
	Settings.Height = FMath::Clamp(Settings.Height, 0.0f, 1.0f);
	Settings.OffsetX = FMath::Clamp(Settings.OffsetX, -1.5f, 1.5f);
	Settings.OffsetY = FMath::Clamp(Settings.OffsetY, -1.5f, 1.5f);

	if (Settings.Style != TEXT("Basic")
		&& Settings.Style != TEXT("Eroded")
		&& Settings.Style != TEXT("Old")
		&& Settings.Style != TEXT("Alpine")
		&& Settings.Style != TEXT("Strata"))
	{
		Settings.Style = TEXT("Basic");
	}
	if (Settings.Bulk != TEXT("Low") && Settings.Bulk != TEXT("Medium") && Settings.Bulk != TEXT("High"))
	{
		Settings.Bulk = TEXT("Medium");
	}

	const double WorldWidthMeters = PhysicalMetrics.HasWorldDimensions() ? PhysicalMetrics.WorldWidthMeters : Settings.WorldSize * 0.01;
	const double WorldDepthMeters = PhysicalMetrics.HasWorldDimensions() ? PhysicalMetrics.WorldDepthMeters : Settings.WorldSize * 0.01;
	const double HalfWidthCm = WorldWidthMeters * 50.0;
	const double HalfDepthCm = WorldDepthMeters * 50.0;
	const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
		FIntPoint(Settings.Resolution, Settings.Resolution),
		FVector2d(-HalfWidthCm, -HalfDepthCm),
		FVector2d(HalfWidthCm, HalfDepthCm));
	if (!Domain.IsValid())
	{
		if (OutError) *OutError = TEXT("Mountain produced an invalid terrain domain.");
		return false;
	}

	FGaeaScalarField Height = MakeField(Domain, GaeaTerrainFieldNames::Height);
	FGaeaScalarField Mass = MakeField(Domain, GaeaTerrainFieldNames::MountainMass);
	FGaeaScalarField Uplift = MakeField(Domain, GaeaTerrainFieldNames::Uplift);
	FGaeaScalarField Ridges = MakeField(Domain, GaeaTerrainFieldNames::RidgeNetwork);
	FGaeaScalarField Drainage = MakeField(Domain, GaeaTerrainFieldNames::DrainageReadiness);
	FGaeaScalarField Erosion = MakeField(Domain, GaeaTerrainFieldNames::ErosionEligibility);
	FGaeaScalarField Rock = MakeField(Domain, GaeaTerrainFieldNames::RockExposure);
	FGaeaScalarField Cryosphere = MakeField(Domain, GaeaTerrainFieldNames::CryosphereEligibility);

	FRandomStream Random(Settings.Seed);
	const FVector2D Center(Settings.OffsetX, Settings.OffsetY);
	const float Radius = FMath::Clamp((0.31f + Settings.Scale * 0.43f) * BulkRadiusMultiplier(Settings.Bulk), 0.18f, 1.18f);
	const int32 SiteCount = Settings.bReduceDetails ? 14 : 26;
	const int32 SummitSiteCount = Settings.bReduceDetails ? 3 : 6;

	TArray<FMountainSite> Sites;
	Sites.Reserve(SiteCount);
	for (int32 Index = 0; Index < SiteCount; ++Index)
	{
		const bool bSummitSite = Index < SummitSiteCount;
		const float SiteRadius = bSummitSite
			? Random.FRandRange(0.015f, 0.16f) * Radius
			: Random.FRandRange(0.16f, 0.92f) * Radius;
		const float Angle = Random.FRandRange(-PI, PI);
		FMountainSite Site;
		Site.Position = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * SiteRadius;
		Site.Weight = Random.FRandRange(bSummitSite ? 0.78f : 0.48f, 1.0f);
		Sites.Add(Site);
	}

	const int32 SpurCount = Settings.bReduceDetails ? 6 : 11;
	TArray<FMountainSpur> Spurs;
	Spurs.Reserve(SpurCount);
	for (int32 Index = 0; Index < SpurCount; ++Index)
	{
		FMountainSpur Spur;
		Spur.Angle = Random.FRandRange(-PI, PI);
		Spur.Length = Random.FRandRange(0.40f, 0.94f) * Radius;
		Spur.Width = Random.FRandRange(0.026f, 0.060f) * FMath::Lerp(0.85f, 1.25f, Settings.Scale * 0.5f);
		Spur.Strength = Random.FRandRange(0.55f, 1.0f);
		Spurs.Add(Spur);
	}

	const float SeedOffset = static_cast<float>(Settings.Seed) * 0.01317f;
	const bool bAlpine = Settings.Style == TEXT("Alpine");
	const bool bEroded = Settings.Style == TEXT("Eroded");
	const bool bOld = Settings.Style == TEXT("Old");
	const bool bStrata = Settings.Style == TEXT("Strata");
	const float VoronoiRidgeWeight = bOld ? 0.25f : (bAlpine ? 0.72f : 0.52f);
	const float SummitWeight = bOld ? 0.47f : (bAlpine ? 0.90f : 0.70f);
	const float SpurWeight = bOld ? 0.20f : (bAlpine ? 0.62f : 0.43f);
	const float WarpStrength = bOld ? 0.055f : (bAlpine ? 0.105f : 0.085f);
	const float FineDetailStrength = Settings.bReduceDetails ? 0.0f : (bOld ? 0.012f : 0.035f);
	float MaxStructural = UE_SMALL_NUMBER;

	for (int32 Y = 0; Y < Settings.Resolution; ++Y)
	{
		const float NY = 2.0f * static_cast<float>(Y) / static_cast<float>(Settings.Resolution - 1) - 1.0f;
		for (int32 X = 0; X < Settings.Resolution; ++X)
		{
			const float NX = 2.0f * static_cast<float>(X) / static_cast<float>(Settings.Resolution - 1) - 1.0f;
			const FVector2D P(NX, NY);

			const float WarpX = FMath::PerlinNoise2D(FVector2D(NX * 2.15f + SeedOffset, NY * 2.15f - SeedOffset * 0.37f));
			const float WarpY = FMath::PerlinNoise2D(FVector2D(NX * 2.15f - SeedOffset * 0.73f, NY * 2.15f + SeedOffset * 0.51f));
			const FVector2D WarpedP = P + FVector2D(WarpX, WarpY) * WarpStrength;
			const FVector2D Local = WarpedP - Center;
			const float Radial = Local.Size();
			const float Theta = FMath::Atan2(Local.Y, Local.X);
			const float BoundaryDistortion = 1.0f
				+ 0.11f * FMath::Sin(Theta * 3.0f + SeedOffset * 0.19f)
				+ 0.065f * FMath::Sin(Theta * 7.0f - SeedOffset * 0.11f)
				+ 0.045f * FMath::PerlinNoise2D(FVector2D(FMath::Cos(Theta) * 2.0f + SeedOffset, FMath::Sin(Theta) * 2.0f));
			const float NormalizedRadius = Radial / FMath::Max(Radius * BoundaryDistortion, 0.05f);
			const float Envelope = Smooth01(1.0f - NormalizedRadius);

			float Nearest = TNumericLimits<float>::Max();
			float SecondNearest = TNumericLimits<float>::Max();
			float NearestWeight = 1.0f;
			for (const FMountainSite& Site : Sites)
			{
				const float Distance = FVector2D::Distance(WarpedP, Site.Position) / FMath::Max(Radius, 0.05f);
				if (Distance < Nearest)
				{
					SecondNearest = Nearest;
					Nearest = Distance;
					NearestWeight = Site.Weight;
				}
				else if (Distance < SecondNearest)
				{
					SecondNearest = Distance;
				}
			}

			// F2-F1 approaches zero on Voronoi boundaries. Invert it to create the
			// branching ridge skeleton described by Gaea's Mountain reference.
			const float BoundaryDistance = FMath::Max(SecondNearest - Nearest, 0.0f);
			const float VoronoiRidge = Smooth01(1.0f - BoundaryDistance / (bAlpine ? 0.20f : 0.16f));
			const float CellCore = FMath::Exp(-FMath::Pow(Nearest / 0.30f, 1.55f)) * NearestWeight;

			float SummitCluster = 0.0f;
			for (int32 SiteIndex = 0; SiteIndex < SummitSiteCount; ++SiteIndex)
			{
				const FMountainSite& Site = Sites[SiteIndex];
				const float D = FVector2D::Distance(WarpedP, Site.Position) / FMath::Max(Radius, 0.05f);
				SummitCluster = FMath::Max(SummitCluster, FMath::Exp(-FMath::Pow(D / (bAlpine ? 0.19f : 0.24f), 1.45f)) * Site.Weight);
			}

			float SpurRidges = 0.0f;
			for (const FMountainSpur& Spur : Spurs)
			{
				const FVector2D End = Center + FVector2D(FMath::Cos(Spur.Angle), FMath::Sin(Spur.Angle)) * Spur.Length;
				float Along = 0.0f;
				const float Distance = DistanceToSegment(WarpedP, Center, End, Along);
				const float RidgeCrossSection = FMath::Exp(-FMath::Pow(Distance / FMath::Max(Spur.Width, 0.005f), bAlpine ? 1.25f : 1.55f));
				const float Taper = FMath::Pow(FMath::Max(1.0f - Along, 0.0f), 0.30f);
				SpurRidges = FMath::Max(SpurRidges, RidgeCrossSection * Taper * Spur.Strength);
			}

			const float Bulk = BulkBase(Settings.Bulk) * FMath::Pow(Envelope, bOld ? 0.72f : 0.92f);
			float Structural = Bulk
				+ Envelope * (VoronoiRidge * VoronoiRidgeWeight
					+ SpurRidges * SpurWeight
					+ SummitCluster * SummitWeight
					+ CellCore * 0.16f);

			if (bEroded || bAlpine)
			{
				const float ValleyPattern = 0.5f + 0.5f * FMath::Sin(Theta * (bAlpine ? 12.0f : 9.0f) + WarpX * 3.0f);
				const float InterRidge = (1.0f - VoronoiRidge) * (1.0f - SpurRidges);
				const float ValleyCut = Envelope * InterRidge * Smooth01(NormalizedRadius) * ValleyPattern;
				Structural -= ValleyCut * (bAlpine ? 0.095f : 0.14f);
			}

			if (FineDetailStrength > 0.0f)
			{
				const float DetailA = FMath::PerlinNoise2D(FVector2D(NX * 18.0f + SeedOffset * 1.3f, NY * 18.0f - SeedOffset * 0.9f));
				const float DetailB = FMath::PerlinNoise2D(FVector2D(NX * 43.0f - SeedOffset * 0.4f, NY * 43.0f + SeedOffset * 0.7f));
				Structural += Envelope * (DetailA * 0.65f + DetailB * 0.35f) * FineDetailStrength;
			}

			Structural = FMath::Max(Structural, 0.0f);
			if (bOld)
			{
				Structural = FMath::Pow(Structural, 0.80f);
			}
			else if (bAlpine)
			{
				Structural = FMath::Pow(Structural, 1.18f);
			}
			else if (bStrata)
			{
				const float Quantized = FMath::FloorToFloat(Structural * 18.0f) / 18.0f;
				Structural = FMath::Lerp(Structural, Quantized, 0.28f);
			}

			MaxStructural = FMath::Max(MaxStructural, Structural);
			const float RidgeSemantic = FMath::Clamp(Envelope * (VoronoiRidge * 0.62f + SpurRidges * 0.38f), 0.0f, 1.0f);
			const float MidSlope = Smooth01(Envelope * 1.65f) * (1.0f - Smooth01((Envelope - 0.70f) * 3.0f));

			// Keep the unrestricted structural value until the whole massif has been
			// evaluated. Per-sample saturation here creates flat mesa-like summits.
			Height.AtInterior(X, Y) = Structural;
			Mass.AtInterior(X, Y) = Envelope;
			Uplift.AtInterior(X, Y) = Structural;
			Ridges.AtInterior(X, Y) = RidgeSemantic;
			Drainage.AtInterior(X, Y) = FMath::Clamp(MidSlope * 0.55f + RidgeSemantic * 0.45f, 0.0f, 1.0f);
			Erosion.AtInterior(X, Y) = FMath::Clamp(MidSlope * 0.58f + RidgeSemantic * 0.42f, 0.0f, 1.0f);
			Rock.AtInterior(X, Y) = RidgeSemantic;
			Cryosphere.AtInterior(X, Y) = 0.0f;
		}
	}

	// Height is a target peak, not a multiplier on an arbitrary random maximum.
	// Normalize the complete structural field so at least one summit reaches the
	// requested Height without flattening an entire saturated cap.
	const float PeakScale = MaxStructural > UE_SMALL_NUMBER ? Settings.Height / MaxStructural : 0.0f;
	for (int32 Y = 0; Y < Settings.Resolution; ++Y)
	{
		for (int32 X = 0; X < Settings.Resolution; ++X)
		{
			const float MountainHeight = FMath::Clamp(Height.AtInterior(X, Y) * PeakScale, 0.0f, Settings.Height);
			const float RelativeHeight = Settings.Height > UE_SMALL_NUMBER ? MountainHeight / Settings.Height : 0.0f;
			Height.AtInterior(X, Y) = MountainHeight;
			Uplift.AtInterior(X, Y) = RelativeHeight;
			const float RidgeSemantic = Ridges.AtInterior(X, Y);
			Rock.AtInterior(X, Y) = FMath::Clamp(RelativeHeight * 0.42f + RidgeSemantic * 0.58f, 0.0f, 1.0f);
			Cryosphere.AtInterior(X, Y) = Smooth01((RelativeHeight - 0.52f) / 0.38f) * FMath::Lerp(0.72f, 1.0f, RidgeSemantic);
		}
	}

	FGaeaTerrainDataset Dataset;
	if (!Dataset.SetScalarField(MoveTemp(Height)))
	{
		if (OutError) *OutError = TEXT("Mountain could not publish Height.");
		return false;
	}
	if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Mass))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Uplift))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Ridges))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Drainage))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Erosion))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Rock))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Cryosphere)))
	{
		if (OutError) *OutError = TEXT("Mountain could not publish its semantic landform fields.");
		return false;
	}

	FString ContextError;
	if (!FGaeaTerrainDerivedData::EnsureContext(Dataset, Settings.HeightScale, PhysicalMetrics, &ContextError))
	{
		if (OutError) *OutError = FString::Printf(TEXT("Mountain context derivation failed: %s"), *ContextError);
		return false;
	}

	OutResult.Dataset = MoveTemp(Dataset);
	OutResult.HeightScale = Settings.HeightScale;
	if (OutError) OutError->Reset();
	return true;
}
