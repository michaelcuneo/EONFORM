#include "GaeaTerrainLandformOps.h"

#include "GaeaGridDomain.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainFieldNames.h"

namespace
{
	struct FBranch
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		FVector2D C = FVector2D::ZeroVector;
		float WidthA = 0.08f;
		float WidthC = 0.02f;
		float Strength = 1.0f;
	};

	struct FPeak
	{
		FVector2D Position = FVector2D::ZeroVector;
		float Radius = 0.12f;
		float Strength = 1.0f;
	};

	FGaeaScalarField MakeField(const FGaeaGridDomain& Domain, FName Name)
	{
		FGaeaFieldDescriptor D;
		D.Name = Name;
		D.Unit = EGaeaFieldUnit::Normalized;
		D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField F;
		F.Initialize(Domain, D, 0.0f);
		return F;
	}

	float Smooth01(float X)
	{
		X = FMath::Clamp(X, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}

	float DistanceToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B, float& T)
	{
		const FVector2D AB = B - A;
		const float L2 = AB.SizeSquared();
		if (L2 <= UE_SMALL_NUMBER)
		{
			T = 0.0f;
			return FVector2D::Distance(P, A);
		}
		T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L2, 0.0f, 1.0f);
		return FVector2D::Distance(P, A + AB * T);
	}

	float SampleBranch(const FVector2D& P, const FBranch& B)
	{
		float T0 = 0.0f;
		float T1 = 0.0f;
		const float D0 = DistanceToSegment(P, B.A, B.B, T0);
		const float D1 = DistanceToSegment(P, B.B, B.C, T1);
		const float Along0 = T0 * 0.5f;
		const float Along1 = 0.5f + T1 * 0.5f;
		const float W0 = FMath::Lerp(B.WidthA, B.WidthC, Along0);
		const float W1 = FMath::Lerp(B.WidthA, B.WidthC, Along1);
		const float S0 = FMath::Exp(-FMath::Pow(D0 / FMath::Max(W0, 0.001f), 1.65f)) * FMath::Pow(1.0f - Along0, 0.35f);
		const float S1 = FMath::Exp(-FMath::Pow(D1 / FMath::Max(W1, 0.001f), 1.65f)) * FMath::Pow(1.0f - Along1, 0.35f);
		return FMath::Max(S0, S1) * B.Strength;
	}

	float SamplePeak(const FVector2D& P, const FPeak& Peak, float Power)
	{
		const float D = FVector2D::Distance(P, Peak.Position) / FMath::Max(Peak.Radius, 0.001f);
		return FMath::Exp(-FMath::Pow(D, Power)) * Peak.Strength;
	}

	float Fbm(const FVector2D& P, float SeedOffset, int32 Octaves, float Lacunarity, float Gain)
	{
		float Frequency = 1.0f;
		float Amplitude = 1.0f;
		float Sum = 0.0f;
		float Weight = 0.0f;
		for (int32 O = 0; O < Octaves; ++O)
		{
			const FVector2D Q(
				P.X * Frequency + SeedOffset * (0.73f + 0.17f * O),
				P.Y * Frequency - SeedOffset * (0.41f + 0.13f * O));
			Sum += FMath::PerlinNoise2D(Q) * Amplitude;
			Weight += Amplitude;
			Frequency *= Lacunarity;
			Amplitude *= Gain;
		}
		return Weight > UE_SMALL_NUMBER ? Sum / Weight : 0.0f;
	}

	float RidgedFbm(const FVector2D& P, float SeedOffset, int32 Octaves, float Frequency)
	{
		float F = Frequency;
		float A = 1.0f;
		float Sum = 0.0f;
		float Weight = 0.0f;
		float Previous = 1.0f;
		for (int32 O = 0; O < Octaves; ++O)
		{
			const FVector2D Q(
				P.X * F + SeedOffset * (0.91f + 0.21f * O),
				P.Y * F - SeedOffset * (0.57f + 0.19f * O));
			float R = 1.0f - FMath::Abs(FMath::PerlinNoise2D(Q));
			R *= R;
			R *= FMath::Lerp(0.55f, 1.0f, Previous);
			Sum += R * A;
			Weight += A;
			Previous = R;
			F *= 2.03f;
			A *= 0.49f;
		}
		return Weight > UE_SMALL_NUMBER ? Sum / Weight : 0.0f;
	}

	float BulkRadius(FName Bulk)
	{
		if (Bulk == TEXT("Low")) return 0.88f;
		if (Bulk == TEXT("High")) return 1.14f;
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

	if (Settings.Style != TEXT("Basic") && Settings.Style != TEXT("Eroded") && Settings.Style != TEXT("Old")
		&& Settings.Style != TEXT("Alpine") && Settings.Style != TEXT("Strata")) Settings.Style = TEXT("Basic");
	if (Settings.Bulk != TEXT("Low") && Settings.Bulk != TEXT("Medium") && Settings.Bulk != TEXT("High")) Settings.Bulk = TEXT("Medium");

	const double WorldWidthMeters = PhysicalMetrics.HasWorldDimensions() ? PhysicalMetrics.WorldWidthMeters : Settings.WorldSize * 0.01;
	const double WorldDepthMeters = PhysicalMetrics.HasWorldDimensions() ? PhysicalMetrics.WorldDepthMeters : Settings.WorldSize * 0.01;
	const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
		FIntPoint(Settings.Resolution, Settings.Resolution),
		FVector2d(WorldWidthMeters * -50.0, WorldDepthMeters * -50.0),
		FVector2d(WorldWidthMeters * 50.0, WorldDepthMeters * 50.0));
	if (!Domain.IsValid())
	{
		if (OutError) *OutError = TEXT("Mountain produced an invalid domain.");
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
	const float SeedOffset = static_cast<float>(Settings.Seed) * 0.014731f;

	const float ReferenceMeters = static_cast<float>(FMath::Max(FMath::Min(WorldWidthMeters, WorldDepthMeters), 1.0));
	const float AspectX = static_cast<float>(WorldWidthMeters / ReferenceMeters);
	const float AspectY = static_cast<float>(WorldDepthMeters / ReferenceMeters);
	const FVector2D Center(Settings.OffsetX * AspectX, Settings.OffsetY * AspectY);
	const float Radius = FMath::Clamp((0.31f + 0.40f * Settings.Scale) * BulkRadius(Settings.Bulk), 0.20f, 1.15f);
	const FVector2D Summit = Center + FVector2D(Random.FRandRange(-0.045f, 0.045f), Random.FRandRange(-0.045f, 0.045f)) * Radius;

	TArray<FPeak> Peaks;
	TArray<FBranch> RidgesGraph;
	TArray<FBranch> ValleyGraph;
	Peaks.Reserve(Settings.bReduceDetails ? 5 : 14);
	RidgesGraph.Reserve(Settings.bReduceDetails ? 8 : 28);
	ValleyGraph.Reserve(Settings.bReduceDetails ? 6 : 18);

	FPeak Main;
	Main.Position = Summit;
	Main.Radius = Radius * (bAlpine ? 0.15f : bOld ? 0.24f : 0.19f);
	Main.Strength = 1.0f;
	Peaks.Add(Main);

	const int32 PrimaryCount = Settings.bReduceDetails ? 6 : (bAlpine ? 10 : 8);
	const float BaseAngle = Random.FRandRange(-PI, PI);
	for (int32 I = 0; I < PrimaryCount; ++I)
	{
		const float Angle = BaseAngle + 2.0f * PI * static_cast<float>(I) / static_cast<float>(PrimaryCount) + Random.FRandRange(-0.20f, 0.20f);
		const float MidAngle = Angle + Random.FRandRange(-0.20f, 0.20f);
		const float EndAngle = Angle + Random.FRandRange(-0.34f, 0.34f);
		const float MidRadius = Radius * Random.FRandRange(0.34f, 0.52f);
		const float EndRadius = Radius * Random.FRandRange(0.76f, 1.02f);

		FBranch Primary;
		Primary.A = Summit;
		Primary.B = Center + FVector2D(FMath::Cos(MidAngle), FMath::Sin(MidAngle)) * MidRadius;
		Primary.C = Center + FVector2D(FMath::Cos(EndAngle), FMath::Sin(EndAngle)) * EndRadius;
		Primary.WidthA = Radius * (bAlpine ? 0.060f : 0.075f);
		Primary.WidthC = Radius * (bAlpine ? 0.018f : 0.028f);
		Primary.Strength = Random.FRandRange(0.72f, 1.0f);
		RidgesGraph.Add(Primary);

		FPeak Shoulder;
		Shoulder.Position = FMath::Lerp(Primary.A, Primary.B, Random.FRandRange(0.58f, 0.90f));
		Shoulder.Radius = Radius * Random.FRandRange(0.075f, 0.14f);
		Shoulder.Strength = Random.FRandRange(0.38f, bAlpine ? 0.68f : 0.58f);
		Peaks.Add(Shoulder);

		if (!Settings.bReduceDetails)
		{
			for (int32 S = 0; S < 2; ++S)
			{
				const float Sign = S == 0 ? -1.0f : 1.0f;
				if (Random.FRand() < 0.25f) continue;
				const float BranchAngle = EndAngle + Sign * Random.FRandRange(0.30f, 0.68f);
				FBranch Secondary;
				Secondary.A = FMath::Lerp(Primary.A, Primary.B, Random.FRandRange(0.62f, 0.95f));
				Secondary.B = Secondary.A + FVector2D(FMath::Cos(BranchAngle), FMath::Sin(BranchAngle)) * Radius * Random.FRandRange(0.12f, 0.24f);
				Secondary.C = Center + FVector2D(FMath::Cos(BranchAngle), FMath::Sin(BranchAngle)) * Radius * Random.FRandRange(0.58f, 0.88f);
				Secondary.WidthA = Radius * 0.032f;
				Secondary.WidthC = Radius * 0.010f;
				Secondary.Strength = Random.FRandRange(0.34f, 0.58f);
				RidgesGraph.Add(Secondary);
			}
		}

		const float NextAngle = BaseAngle + 2.0f * PI * static_cast<float>(I + 1) / static_cast<float>(PrimaryCount);
		const float ValleyAngle = 0.5f * (Angle + NextAngle) + Random.FRandRange(-0.12f, 0.12f);
		FBranch Valley;
		Valley.A = Summit + FVector2D(FMath::Cos(ValleyAngle), FMath::Sin(ValleyAngle)) * Radius * Random.FRandRange(0.14f, 0.23f);
		Valley.B = Center + FVector2D(FMath::Cos(ValleyAngle + Random.FRandRange(-0.16f, 0.16f)), FMath::Sin(ValleyAngle + Random.FRandRange(-0.16f, 0.16f))) * Radius * Random.FRandRange(0.45f, 0.62f);
		Valley.C = Center + FVector2D(FMath::Cos(ValleyAngle + Random.FRandRange(-0.26f, 0.26f)), FMath::Sin(ValleyAngle + Random.FRandRange(-0.26f, 0.26f))) * Radius * Random.FRandRange(0.86f, 1.05f);
		Valley.WidthA = Radius * 0.026f;
		Valley.WidthC = Radius * 0.075f;
		Valley.Strength = Random.FRandRange(0.65f, 1.0f);
		ValleyGraph.Add(Valley);
	}

	float MaxRaw = UE_SMALL_NUMBER;
	for (int32 Y = 0; Y < Settings.Resolution; ++Y)
	{
		const float V = 2.0f * static_cast<float>(Y) / static_cast<float>(Settings.Resolution - 1) - 1.0f;
		for (int32 X = 0; X < Settings.Resolution; ++X)
		{
			const float U = 2.0f * static_cast<float>(X) / static_cast<float>(Settings.Resolution - 1) - 1.0f;
			FVector2D P(U * AspectX, V * AspectY);

			const float WarpA = Fbm(P * 1.8f, SeedOffset, 3, 2.0f, 0.5f);
			const float WarpB = Fbm(P * 1.8f + FVector2D(17.31f, -9.73f), SeedOffset + 19.0f, 3, 2.0f, 0.5f);
			P += FVector2D(WarpA, WarpB) * Radius * (bAlpine ? 0.060f : 0.045f);

			const FVector2D Local = P - Center;
			const float R = Local.Size();
			const float Theta = FMath::Atan2(Local.Y, Local.X);
			const float Boundary = 1.0f
				+ 0.070f * FMath::Sin(Theta * 3.0f + SeedOffset)
				+ 0.035f * FMath::Sin(Theta * 7.0f - SeedOffset * 0.7f)
				+ 0.025f * Fbm(FVector2D(FMath::Cos(Theta), FMath::Sin(Theta)) * 2.0f, SeedOffset, 2, 2.1f, 0.5f);
			const float Envelope = Smooth01(1.0f - R / FMath::Max(Radius * Boundary, 0.01f));
			if (Envelope <= 0.0f) continue;

			float PeakField = 0.0f;
			for (int32 PIndex = 0; PIndex < Peaks.Num(); ++PIndex)
			{
				PeakField = FMath::Max(PeakField, SamplePeak(P, Peaks[PIndex], PIndex == 0 ? (bAlpine ? 1.35f : 1.55f) : 1.7f));
			}

			float RidgeField = 0.0f;
			for (const FBranch& B : RidgesGraph) RidgeField = FMath::Max(RidgeField, SampleBranch(P, B));
			float ValleyField = 0.0f;
			for (const FBranch& B : ValleyGraph) ValleyField = FMath::Max(ValleyField, SampleBranch(P, B));

			const float MacroRidges = RidgedFbm(P, SeedOffset + 7.0f, Settings.bReduceDetails ? 3 : 5, 2.4f);
			const float MesoRidges = RidgedFbm(P, SeedOffset + 37.0f, Settings.bReduceDetails ? 2 : 5, 7.5f);
			const float FineRidges = Settings.bReduceDetails ? 0.0f : RidgedFbm(P, SeedOffset + 71.0f, 4, 22.0f);
			const float Fractal = Fbm(P, SeedOffset + 101.0f, Settings.bReduceDetails ? 3 : 6, 2.03f, 0.48f);

			const float BulkBase = Settings.Bulk == TEXT("Low") ? 0.27f : Settings.Bulk == TEXT("High") ? 0.43f : 0.35f;
			float H = FMath::Pow(Envelope, bOld ? 0.72f : bAlpine ? 1.08f : 0.92f) * BulkBase;
			H += Envelope * PeakField * (bAlpine ? 0.62f : 0.52f);
			H += Envelope * RidgeField * (bAlpine ? 0.30f : 0.23f);
			H += Envelope * (MacroRidges - 0.48f) * (bOld ? 0.055f : 0.10f);
			H += Envelope * (MesoRidges - 0.48f) * (bAlpine ? 0.075f : 0.052f);
			H += Envelope * (FineRidges - 0.48f) * (bAlpine ? 0.026f : 0.016f);
			H += Envelope * Fractal * (bOld ? 0.018f : 0.032f);
			H -= Envelope * ValleyField * (bEroded ? 0.20f : bAlpine ? 0.17f : 0.12f);

			if (bStrata) H += FMath::Sin(H * PI * 24.0f + SeedOffset * 0.11f) * Envelope * 0.012f;
			H = FMath::Max(H, 0.0f);
			MaxRaw = FMath::Max(MaxRaw, H);

			Height.AtInterior(X, Y) = H;
			Mass.AtInterior(X, Y) = Envelope;
			Ridges.AtInterior(X, Y) = FMath::Clamp(RidgeField * 0.65f + MacroRidges * 0.20f + MesoRidges * 0.15f, 0.0f, 1.0f) * Envelope;
			Drainage.AtInterior(X, Y) = FMath::Clamp(ValleyField * 0.65f + (1.0f - RidgeField) * Envelope * 0.35f, 0.0f, 1.0f);
			Erosion.AtInterior(X, Y) = FMath::Clamp(ValleyField * 0.45f + RidgeField * 0.25f + Envelope * 0.30f, 0.0f, 1.0f);
		}
	}

	const float ScaleToRequestedPeak = MaxRaw > UE_SMALL_NUMBER ? Settings.Height / MaxRaw : 0.0f;
	for (int32 Y = 0; Y < Settings.Resolution; ++Y)
	{
		for (int32 X = 0; X < Settings.Resolution; ++X)
		{
			const float H = FMath::Clamp(Height.AtInterior(X, Y) * ScaleToRequestedPeak, 0.0f, Settings.Height);
			const float H01 = Settings.Height > UE_SMALL_NUMBER ? H / Settings.Height : 0.0f;
			Height.AtInterior(X, Y) = H;
			Uplift.AtInterior(X, Y) = H01 * Mass.AtInterior(X, Y);
			Rock.AtInterior(X, Y) = FMath::Clamp(Ridges.AtInterior(X, Y) * 0.62f + H01 * 0.38f, 0.0f, 1.0f);
			Cryosphere.AtInterior(X, Y) = Smooth01((H01 - 0.56f) / 0.34f) * FMath::Lerp(0.72f, 1.0f, Ridges.AtInterior(X, Y));
		}
	}

	FGaeaTerrainDataset Dataset;
	if (!Dataset.SetScalarField(MoveTemp(Height))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Mass))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Uplift))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Ridges))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Drainage))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Erosion))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Rock))
		|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Cryosphere)))
	{
		if (OutError) *OutError = TEXT("Mountain could not publish its terrain fields.");
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
