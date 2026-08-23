#include "GaeaTerrainLandformOps.h"

#include "GaeaGridDomain.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainFieldNames.h"

namespace
{
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
	Settings.Radius = FMath::Clamp(Settings.Radius, 0.05f, 1.5f);
	Settings.Elongation = FMath::Clamp(Settings.Elongation, 0.05f, 1.0f);
	Settings.PeakSharpness = FMath::Clamp(Settings.PeakSharpness, 0.0f, 1.0f);
	Settings.RidgeStrength = FMath::Clamp(Settings.RidgeStrength, 0.0f, 1.0f);
	Settings.RidgeFrequency = FMath::Clamp(Settings.RidgeFrequency, 0.25f, 16.0f);
	Settings.Roughness = FMath::Clamp(Settings.Roughness, 0.0f, 1.0f);
	Settings.Asymmetry = FMath::Clamp(Settings.Asymmetry, -1.0f, 1.0f);
	Settings.BaseElevation = FMath::Clamp(Settings.BaseElevation, -1.0f, 1.0f);

	const double WorldWidth = PhysicalMetrics.HasWorldDimensions() ? PhysicalMetrics.WorldWidthMeters : Settings.WorldSize;
	const double WorldDepth = PhysicalMetrics.HasWorldDimensions() ? PhysicalMetrics.WorldDepthMeters : Settings.WorldSize;
	const double HalfWidthCm = WorldWidth * 50.0;
	const double HalfDepthCm = WorldDepth * 50.0;
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

	const float Angle = FMath::DegreesToRadians(Settings.OrientationDegrees);
	const FVector2D Axis(FMath::Cos(Angle), FMath::Sin(Angle));
	const FVector2D Side(-Axis.Y, Axis.X);
	const float PeakPower = FMath::Lerp(1.15f, 4.5f, Settings.PeakSharpness);
	const float SeedOffset = static_cast<float>(Settings.Seed) * 0.01371f;

	for (int32 Y = 0; Y < Settings.Resolution; ++Y)
	{
		const float NY = Settings.Resolution > 1 ? (2.0f * Y / static_cast<float>(Settings.Resolution - 1) - 1.0f) : 0.0f;
		for (int32 X = 0; X < Settings.Resolution; ++X)
		{
			const float NX = Settings.Resolution > 1 ? (2.0f * X / static_cast<float>(Settings.Resolution - 1) - 1.0f) : 0.0f;
			const FVector2D P(NX, NY);
			const float Along = FVector2D::DotProduct(P, Axis);
			const float Across = FVector2D::DotProduct(P, Side) / FMath::Max(Settings.Elongation, 0.05f);
			const float Radial = FMath::Sqrt(Along * Along + Across * Across) / Settings.Radius;
			const float Envelope = Smooth01(1.0f - Radial);

			const FVector2D WarpP(
				NX * 1.7f + SeedOffset,
				NY * 1.7f - SeedOffset * 0.63f);
			const float MacroWarp = FMath::PerlinNoise2D(WarpP) * 0.16f * Settings.Roughness;
			const float RidgeCoordinate = (Across + MacroWarp) * Settings.RidgeFrequency;
			const float RidgeNoise = FMath::PerlinNoise2D(FVector2D(
				Along * Settings.RidgeFrequency * 0.72f + SeedOffset * 0.31f,
				RidgeCoordinate + SeedOffset));
			const float Ridge = FMath::Pow(1.0f - FMath::Abs(RidgeNoise), FMath::Lerp(2.6f, 0.72f, Settings.RidgeStrength));

			const float Secondary = 0.5f + 0.5f * FMath::PerlinNoise2D(FVector2D(
				NX * 7.0f + SeedOffset * 1.91f,
				NY * 7.0f - SeedOffset * 1.27f));
			const float Detail = FMath::Lerp(1.0f, FMath::Lerp(0.72f, 1.18f, Secondary), Settings.Roughness);
			const float AsymmetryBias = FMath::Clamp(1.0f + Settings.Asymmetry * Across * 0.55f, 0.35f, 1.65f);
			const float BaseMass = FMath::Pow(Envelope, PeakPower);
			const float StructuralUplift = FMath::Clamp(BaseMass * AsymmetryBias, 0.0f, 1.0f);
			const float RidgeContribution = StructuralUplift * Ridge * Settings.RidgeStrength;
			const float MountainHeight = FMath::Clamp(
				Settings.BaseElevation + StructuralUplift * FMath::Lerp(0.72f, 1.0f, RidgeContribution) * Detail,
				-1.0f,
				1.0f);

			Height.AtInterior(X, Y) = MountainHeight;
			Mass.AtInterior(X, Y) = Envelope;
			Uplift.AtInterior(X, Y) = StructuralUplift;
			Ridges.AtInterior(X, Y) = FMath::Clamp(RidgeContribution, 0.0f, 1.0f);

			const float MidSlopeBand = Smooth01(Envelope * 1.7f) * (1.0f - Smooth01((Envelope - 0.62f) * 2.6f));
			Drainage.AtInterior(X, Y) = FMath::Clamp(MidSlopeBand * 0.62f + RidgeContribution * 0.38f, 0.0f, 1.0f);
			Erosion.AtInterior(X, Y) = FMath::Clamp(StructuralUplift * 0.52f + MidSlopeBand * 0.48f, 0.0f, 1.0f);
			Rock.AtInterior(X, Y) = FMath::Clamp(StructuralUplift * 0.48f + RidgeContribution * 0.52f, 0.0f, 1.0f);
			Cryosphere.AtInterior(X, Y) = Smooth01((MountainHeight - 0.52f) / 0.38f) * FMath::Lerp(0.7f, 1.0f, RidgeContribution);
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
