#include "GaeaTerrainLandformOps.h"

#include "GaeaGridDomain.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainFieldNames.h"

namespace
{
	struct FMountainPeak
	{
		FVector2D Position = FVector2D::ZeroVector;
		float Radius = 0.1f;
		float Strength = 1.0f;
	};

	struct FMountainBranch
	{
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D Mid = FVector2D::ZeroVector;
		FVector2D End = FVector2D::ZeroVector;
		float WidthStart = 0.05f;
		float WidthEnd = 0.02f;
		float Strength = 1.0f;
	};

	struct FMountainSite
	{
		FVector2D Position = FVector2D::ZeroVector;
		float Weight = 1.0f;
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

	float SampleBranch(const FVector2D& Point, const FMountainBranch& Branch, float CrossSectionPower)
	{
		float T0 = 0.0f;
		const float D0 = DistanceToSegment(Point, Branch.Start, Branch.Mid, T0);
		const float W0 = FMath::Lerp(Branch.WidthStart, FMath::Lerp(Branch.WidthStart, Branch.WidthEnd, 0.48f), T0);
		const float S0 = FMath::Exp(-FMath::Pow(D0 / FMath::Max(W0, 0.001f), CrossSectionPower));

		float T1 = 0.0f;
		const float D1 = DistanceToSegment(Point, Branch.Mid, Branch.End, T1);
		const float W1 = FMath::Lerp(FMath::Lerp(Branch.WidthStart, Branch.WidthEnd, 0.48f), Branch.WidthEnd, T1);
		const float S1 = FMath::Exp(-FMath::Pow(D1 / FMath::Max(W1, 0.001f), CrossSectionPower));
		const float Along = D0 <= D1 ? T0 * 0.5f : 0.5f + T1 * 0.5f;
		const float Taper = FMath::Pow(FMath::Max(1.0f - Along, 0.0f), 0.34f);
		return FMath::Max(S0, S1) * Taper * Branch.Strength;
	}

	float SamplePeak(const FVector2D& Point, const FMountainPeak& Peak, float Power)
	{
		const float D = FVector2D::Distance(Point, Peak.Position) / FMath::Max(Peak.Radius, 0.001f);
		return FMath::Exp(-FMath::Pow(D, Power)) * Peak.Strength;
	}

	float RidgedFbm(const FVector2D& Point, float SeedOffset, int32 Octaves)
	{
		float Frequency = 1.0f;
		float Amplitude = 0.58f;
		float Sum = 0.0f;
		float WeightSum = 0.0f;
		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			const FVector2D P(
				Point.X * Frequency + SeedOffset * (0.73f + Octave * 0.17f),
				Point.Y * Frequency - SeedOffset * (0.41f + Octave * 0.13f));
			const float N = FMath::PerlinNoise2D(P);
			const float Ridge = 1.0f - FMath::Abs(N);
			Sum += Ridge * Amplitude;
			WeightSum += Amplitude;
			Frequency *= 2.07f;
			Amplitude *= 0.49f;
		}
		return WeightSum > UE_SMALL_NUMBER ? Sum / WeightSum : 0.0f;
	}

	float BulkRadiusMultiplier(FName Bulk)
	{
		if (Bulk == TEXT("Low")) return 0.86f;
		if (Bulk == TEXT("High")) return 1.14f;
		return 1.0f;
	}

	float BulkMassWeight(FName Bulk)
	{
		if (Bulk == TEXT("Low")) return 0.31f;
		if (Bulk == TEXT("High")) return 0.48f;
		return 0.39f;
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

	const bool bAlpine = Settings.Style == TEXT("Alpine");
	const bool bEroded = Settings.Style == TEXT("Eroded");
	const bool bOld = Settings.Style == TEXT("Old");
	const bool bStrata = Settings.Style == TEXT("Strata");

	FRandomStream Random(Settings.Seed);
	const float SeedOffset = static_cast<float>(Settings.Seed) * 0.01317f;
	const float ReferenceMeters = static_cast<float>(FMath::Max(FMath::Min(WorldWidthMeters, WorldDepthMeters), 1.0));
	const float AspectX = static_cast<float>(WorldWidthMeters / ReferenceMeters);
	const float AspectY = static_cast<float>(WorldDepthMeters / ReferenceMeters);
	const FVector2D Center(Settings.OffsetX * AspectX, Settings.OffsetY * AspectY);
	const float Radius = FMath::Clamp((0.34f + Settings.Scale * 0.40f) * BulkRadiusMultiplier(Settings.Bulk), 0.20f, 1.20f);

	// The mountain starts with a real dominant summit. Secondary peaks then sit on
	// a branching ridge tree. Voronoi only modulates those structures; its cell
	// boundaries are never added directly as elevation, which avoids crown/ring terrain.
	const FVector2D SummitOffset(
		Random.FRandRange(-0.055f, 0.055f) * Radius,
		Random.FRandRange(-0.055f, 0.055f) * Radius);
	const FVector2D SummitCenter = Center + SummitOffset;

	TArray<FMountainPeak> Peaks;
	Peaks.Reserve(Settings.bReduceDetails ? 4 : 8);
	FMountainPeak DominantPeak;
	DominantPeak.Position = SummitCenter;
	DominantPeak.Radius = Radius * (bAlpine ? 0.185f : (bOld ? 0.27f : 0.225f));
	DominantPeak.Strength = 1.0f;
	Peaks.Add(DominantPeak);

	const int32 PrimaryCount = Settings.bReduceDetails
		? (bOld ? 4 : 5)
		: (bAlpine ? 8 : (bOld ? 5 : 7));
	const float BaseAngle = Random.FRandRange(-PI, PI);
	TArray<float> PrimaryAngles;
	PrimaryAngles.Reserve(PrimaryCount);
	TArray<FMountainBranch> PrimaryRidges;
	PrimaryRidges.Reserve(PrimaryCount);
	TArray<FMountainBranch> SecondaryRidges;
	SecondaryRidges.Reserve(Settings.bReduceDetails ? PrimaryCount / 2 : PrimaryCount + 2);
	TArray<FMountainBranch> Valleys;
	Valleys.Reserve(PrimaryCount);

	for (int32 Index = 0; Index < PrimaryCount; ++Index)
	{
		const float EvenAngle = BaseAngle + (2.0f * PI * static_cast<float>(Index) / static_cast<float>(PrimaryCount));
		const float Angle = EvenAngle + Random.FRandRange(-0.22f, 0.22f);
		PrimaryAngles.Add(Angle);

		const float MidRadius = Random.FRandRange(0.34f, 0.52f) * Radius;
		const float EndRadius = Random.FRandRange(0.76f, 0.98f) * Radius;
		const float MidAngle = Angle + Random.FRandRange(-0.18f, 0.18f);
		const float EndAngle = Angle + Random.FRandRange(-0.31f, 0.31f);

		FMountainBranch Ridge;
		Ridge.Start = SummitCenter;
		Ridge.Mid = Center + FVector2D(FMath::Cos(MidAngle), FMath::Sin(MidAngle)) * MidRadius;
		Ridge.End = Center + FVector2D(FMath::Cos(EndAngle), FMath::Sin(EndAngle)) * EndRadius;
		Ridge.WidthStart = Radius * (bAlpine ? 0.058f : 0.071f);
		Ridge.WidthEnd = Radius * (bAlpine ? 0.021f : 0.030f);
		Ridge.Strength = Random.FRandRange(0.70f, 1.0f);
		PrimaryRidges.Add(Ridge);

		if (!Settings.bReduceDetails || (Index % 2) == 0)
		{
			FMountainPeak SecondaryPeak;
			SecondaryPeak.Position = FMath::Lerp(SummitCenter, Ridge.Mid, Random.FRandRange(0.58f, 0.92f));
			SecondaryPeak.Radius = Radius * Random.FRandRange(bAlpine ? 0.085f : 0.105f, bAlpine ? 0.135f : 0.165f);
			SecondaryPeak.Strength = Random.FRandRange(0.42f, bAlpine ? 0.72f : 0.62f);
			Peaks.Add(SecondaryPeak);
		}

		if (!Settings.bReduceDetails && (Index % 2) == 0)
		{
			const float BranchSign = Random.FRand() > 0.5f ? 1.0f : -1.0f;
			const float BranchAngle = EndAngle + BranchSign * Random.FRandRange(0.34f, 0.66f);
			FMountainBranch Branch;
			Branch.Start = Ridge.Mid;
			Branch.Mid = FMath::Lerp(Ridge.Mid, Ridge.End, 0.22f)
				+ FVector2D(FMath::Cos(BranchAngle), FMath::Sin(BranchAngle)) * Radius * 0.12f;
			Branch.End = Center + FVector2D(FMath::Cos(BranchAngle), FMath::Sin(BranchAngle)) * Random.FRandRange(0.68f, 0.92f) * Radius;
			Branch.WidthStart = Radius * 0.034f;
			Branch.WidthEnd = Radius * 0.014f;
			Branch.Strength = Random.FRandRange(0.42f, 0.67f);
			SecondaryRidges.Add(Branch);
		}
	}

	for (int32 Index = 0; Index < PrimaryCount; ++Index)
	{
		const float A0 = PrimaryAngles[Index];
		float A1 = PrimaryAngles[(Index + 1) % PrimaryCount];
		while (A1 < A0) A1 += 2.0f * PI;
		const float ValleyAngle = 0.5f * (A0 + A1) + Random.FRandRange(-0.09f, 0.09f);
		const float MidAngle = ValleyAngle + Random.FRandRange(-0.13f, 0.13f);
		const float EndAngle = ValleyAngle + Random.FRandRange(-0.22f, 0.22f);
		FMountainBranch Valley;
		Valley.Start = SummitCenter + FVector2D(FMath::Cos(ValleyAngle), FMath::Sin(ValleyAngle)) * Radius * Random.FRandRange(0.17f, 0.25f);
		Valley.Mid = Center + FVector2D(FMath::Cos(MidAngle), FMath::Sin(MidAngle)) * Radius * Random.FRandRange(0.48f, 0.62f);
		Valley.End = Center + FVector2D(FMath::Cos(EndAngle), FMath::Sin(EndAngle)) * Radius * Random.FRandRange(0.88f, 1.03f);
		Valley.WidthStart = Radius * (bAlpine ? 0.028f : 0.036f);
		Valley.WidthEnd = Radius * (bAlpine ? 0.064f : 0.078f);
		Valley.Strength = Random.FRandRange(0.65f, 1.0f);
		Valleys.Add(Valley);
	}

	const int32 SiteCount = Settings.bReduceDetails ? 12 : 22;
	TArray<FMountainSite> Sites;
	Sites.Reserve(SiteCount);
	for (int32 Index = 0; Index < SiteCount; ++Index)
	{
		const float Angle = Random.FRandRange(-PI, PI);
		const float SiteRadius = Random.FRandRange(0.08f, 0.98f) * Radius;
		FMountainSite Site;
		Site.Position = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * SiteRadius;
		Site.Weight = Random.FRandRange(0.45f, 1.0f);
		Sites.Add(Site);
	}

	const float MassWeight = BulkMassWeight(Settings.Bulk);
	const float RidgeWeight = bAlpine ? 0.31f : (bOld ? 0.15f : 0.23f);
	const float ValleyWeight = bEroded ? 0.24f : (bAlpine ? 0.20f : (bOld ? 0.10f : 0.14f));
	const float DetailWeight = Settings.bReduceDetails ? 0.0f : (bOld ? 0.018f : (bAlpine ? 0.050f : 0.034f));
	const float WarpStrength = bOld ? 0.030f : (bAlpine ? 0.060f : 0.048f);
	const int32 DetailOctaves = Settings.bReduceDetails ? 2 : 5;
	float MaxStructural = UE_SMALL_NUMBER;

	for (int32 Y = 0; Y < Settings.Resolution; ++Y)
	{
		const float NY = 2.0f * static_cast<float>(Y) / static_cast<float>(Settings.Resolution - 1) - 1.0f;
		for (int32 X = 0; X < Settings.Resolution; ++X)
		{
			const float NX = 2.0f * static_cast<float>(X) / static_cast<float>(Settings.Resolution - 1) - 1.0f;
			FVector2D P(NX * AspectX, NY * AspectY);

			const float WarpX = FMath::PerlinNoise2D(FVector2D(P.X * 1.75f + SeedOffset, P.Y * 1.75f - SeedOffset * 0.37f));
			const float WarpY = FMath::PerlinNoise2D(FVector2D(P.X * 1.75f - SeedOffset * 0.73f, P.Y * 1.75f + SeedOffset * 0.51f));
			P += FVector2D(WarpX, WarpY) * WarpStrength * Radius;

			const FVector2D Local = P - Center;
			const float Radial = Local.Size();
			const float Theta = FMath::Atan2(Local.Y, Local.X);
			const float BoundaryDistortion = 1.0f
				+ 0.075f * FMath::Sin(Theta * 3.0f + SeedOffset * 0.19f)
				+ 0.038f * FMath::Sin(Theta * 7.0f - SeedOffset * 0.11f)
				+ 0.035f * FMath::PerlinNoise2D(FVector2D(FMath::Cos(Theta) * 2.0f + SeedOffset, FMath::Sin(Theta) * 2.0f));
			const float NormalizedRadius = Radial / FMath::Max(Radius * BoundaryDistortion, 0.05f);
			const float Envelope = Smooth01(1.0f - NormalizedRadius);

			if (Envelope <= 0.0f)
			{
				Height.AtInterior(X, Y) = 0.0f;
				Mass.AtInterior(X, Y) = 0.0f;
				Uplift.AtInterior(X, Y) = 0.0f;
				Ridges.AtInterior(X, Y) = 0.0f;
				Drainage.AtInterior(X, Y) = 0.0f;
				Erosion.AtInterior(X, Y) = 0.0f;
				Rock.AtInterior(X, Y) = 0.0f;
				Cryosphere.AtInterior(X, Y) = 0.0f;
				continue;
			}

			float PeakField = 0.0f;
			for (int32 PeakIndex = 0; PeakIndex < Peaks.Num(); ++PeakIndex)
			{
				const float PeakPower = PeakIndex == 0
					? (bAlpine ? 1.35f : (bOld ? 1.85f : 1.58f))
					: (bAlpine ? 1.55f : 1.85f);
				PeakField = FMath::Max(PeakField, SamplePeak(P, Peaks[PeakIndex], PeakPower));
			}

			float PrimaryRidge = 0.0f;
			for (const FMountainBranch& Ridge : PrimaryRidges)
			{
				PrimaryRidge = FMath::Max(PrimaryRidge, SampleBranch(P, Ridge, bAlpine ? 1.38f : 1.62f));
			}
			float SecondaryRidge = 0.0f;
			for (const FMountainBranch& Ridge : SecondaryRidges)
			{
				SecondaryRidge = FMath::Max(SecondaryRidge, SampleBranch(P, Ridge, 1.52f));
			}
			const float RidgeField = FMath::Clamp(PrimaryRidge + SecondaryRidge * 0.62f, 0.0f, 1.0f);

			float ValleyField = 0.0f;
			for (const FMountainBranch& Valley : Valleys)
			{
				ValleyField = FMath::Max(ValleyField, SampleBranch(P, Valley, 1.48f));
			}

			float Nearest = TNumericLimits<float>::Max();
			float SecondNearest = TNumericLimits<float>::Max();
			float NearestWeight = 1.0f;
			for (const FMountainSite& Site : Sites)
			{
				const float D = FVector2D::Distance(P, Site.Position) / FMath::Max(Radius, 0.05f);
				if (D < Nearest)
				{
					SecondNearest = Nearest;
					Nearest = D;
					NearestWeight = Site.Weight;
				}
				else if (D < SecondNearest)
				{
					SecondNearest = D;
				}
			}
			const float BoundaryDistance = FMath::Max(SecondNearest - Nearest, 0.0f);
			const float VoronoiEdge = FMath::Exp(-FMath::Pow(BoundaryDistance / 0.095f, 2.0f));
			const float VoronoiCore = FMath::Exp(-FMath::Pow(Nearest / 0.34f, 1.65f)) * NearestWeight;
			const float VoronoiModulation = FMath::Clamp((VoronoiCore - 0.40f) * 0.045f - VoronoiEdge * 0.026f, -0.035f, 0.035f);

			const float BasePower = bOld ? 0.78f : (bAlpine ? 1.18f : 0.98f);
			const float BaseMass = FMath::Pow(Envelope, BasePower) * MassWeight;
			float Structural = BaseMass
				+ Envelope * PeakField * (bAlpine ? 0.61f : (bOld ? 0.46f : 0.54f))
				+ Envelope * RidgeField * RidgeWeight
				- Envelope * ValleyField * ValleyWeight;

			Structural *= 1.0f + VoronoiModulation;

			if (DetailWeight > 0.0f)
			{
				const float Detail = RidgedFbm(P * 4.2f, SeedOffset, DetailOctaves);
				const float DetailCentered = Detail - 0.62f;
				const float DetailMask = Envelope * FMath::Clamp(0.22f + PeakField * 0.50f + RidgeField * 0.48f, 0.0f, 1.0f);
				Structural += DetailCentered * DetailWeight * DetailMask;
			}

			Structural = FMath::Max(Structural, 0.0f);
			if (bOld)
			{
				Structural = FMath::Pow(Structural, 0.88f);
			}
			else if (bAlpine)
			{
				Structural = FMath::Pow(Structural, 1.06f);
			}
			else if (bStrata)
			{
				const float LayerWave = FMath::Sin(Structural * 2.0f * PI * 15.0f + SeedOffset * 0.03f);
				Structural += LayerWave * 0.010f * Envelope;
				Structural = FMath::Max(Structural, 0.0f);
			}

			MaxStructural = FMath::Max(MaxStructural, Structural);
			Height.AtInterior(X, Y) = Structural;
			Mass.AtInterior(X, Y) = Envelope;
			Uplift.AtInterior(X, Y) = Structural;
			Ridges.AtInterior(X, Y) = RidgeField;
			Drainage.AtInterior(X, Y) = FMath::Clamp(ValleyField * 0.58f + (1.0f - Envelope) * Envelope * 1.35f + RidgeField * 0.18f, 0.0f, 1.0f);
			Erosion.AtInterior(X, Y) = FMath::Clamp(ValleyField * 0.48f + RidgeField * 0.32f + Envelope * 0.20f, 0.0f, 1.0f);
			Rock.AtInterior(X, Y) = FMath::Clamp(RidgeField * 0.72f + PeakField * 0.28f, 0.0f, 1.0f);
			Cryosphere.AtInterior(X, Y) = 0.0f;
		}
	}

	// The dominant summit is broad enough to span multiple samples, so normalizing
	// the complete field to the requested Height cannot manufacture a one-pixel pin.
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
			Rock.AtInterior(X, Y) = FMath::Clamp(Rock.AtInterior(X, Y) * 0.64f + RelativeHeight * 0.36f, 0.0f, 1.0f);
			Cryosphere.AtInterior(X, Y) = Smooth01((RelativeHeight - 0.55f) / 0.34f) * FMath::Lerp(0.72f, 1.0f, RidgeSemantic);
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
