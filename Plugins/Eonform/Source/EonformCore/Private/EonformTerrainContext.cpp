#include "EonformTerrainContext.h"

#include "EonformTerrainFieldNames.h"

namespace
{
	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	FEonformScalarField MakeField(const FEonformGridDomain& Domain, FName Name, EEonformFieldUnit Unit = EEonformFieldUnit::Normalized)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;

		FEonformScalarField Field;
		Field.Initialize(Domain, Descriptor);
		return Field;
	}

	bool CopyCompatibleField(
		const FEonformTerrainDataset& Dataset,
		FName Name,
		const FEonformGridDomain& Domain,
		FEonformScalarField& OutField)
	{
		const FEonformScalarField* Existing = Dataset.FindScalarField(Name);
		if (!Existing || !Existing->IsValid() || Existing->Domain != Domain)
		{
			return false;
		}
		OutField = *Existing;
		return true;
	}

	FIntPoint ResolveReferenceCoordinate(
		const FEonformGridDomain& Domain,
		const FVector2d& World)
	{
		const FVector2d Size = Domain.WorldSize();
		return FIntPoint(
			FMath::Clamp(
				FMath::RoundToInt((World.X - Domain.WorldMin.X) / Size.X * static_cast<double>(Domain.Dimensions.X - 1)),
				0,
				Domain.Dimensions.X - 1),
			FMath::Clamp(
				FMath::RoundToInt((World.Y - Domain.WorldMin.Y) / Size.Y * static_cast<double>(Domain.Dimensions.Y - 1)),
				0,
				Domain.Dimensions.Y - 1));
	}
}

bool FEonformTerrainContext::Analyze(
	const FEonformScalarField& Height,
	float HeightScale,
	FEonformTerrainDataset& InOutDataset,
	FString* OutError)
{
	return Analyze(Height, HeightScale, FEonformTerrainPhysicalMetrics(), InOutDataset, OutError);
}

bool FEonformTerrainContext::Analyze(
	const FEonformScalarField& Height,
	float HeightScale,
	const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
	FEonformTerrainDataset& InOutDataset,
	FString* OutError)
{
	return Analyze(
		Height,
		HeightScale,
		PhysicalMetrics,
		FEonformTerrainContextEvaluationScope(),
		InOutDataset,
		OutError);
}

bool FEonformTerrainContext::Analyze(
	const FEonformScalarField& Height,
	float HeightScale,
	const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
	const FEonformTerrainContextEvaluationScope& EvaluationScope,
	FEonformTerrainDataset& InOutDataset,
	FString* OutError)
{
	auto Fail = [OutError](const TCHAR* Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (!Height.IsValid()) return Fail(TEXT("Terrain Context requires a valid Height field."));
	if (HeightScale <= UE_SMALL_NUMBER) return Fail(TEXT("Terrain Context requires a positive HeightScale."));

	const FIntPoint Dimensions = Height.Domain.Dimensions;
	const FVector2d DomainCellSize = Height.Domain.GetCellSize();
	if (Dimensions.X < 2 || Dimensions.Y < 2 || DomainCellSize.X <= UE_SMALL_NUMBER || DomainCellSize.Y <= UE_SMALL_NUMBER)
	{
		return Fail(TEXT("Terrain Context received an invalid Height domain."));
	}

	const bool bRegional = EvaluationScope.IsRegional();
	if (bRegional && Height.Domain.BorderSamples < 1)
	{
		return Fail(TEXT("Regional Terrain Context requires one scheduler border sample for exact derivatives."));
	}
	if (EvaluationScope.bUseGlobalHeightRange
		&& (!FMath::IsFinite(EvaluationScope.GlobalMinimumHeight)
			|| !FMath::IsFinite(EvaluationScope.GlobalMaximumHeight)
			|| EvaluationScope.GlobalMaximumHeight < EvaluationScope.GlobalMinimumHeight))
	{
		return Fail(TEXT("Terrain Context received an invalid global Height range."));
	}

	const FIntPoint PhysicalResolution = bRegional ? EvaluationScope.ReferenceResolution : Dimensions;
	const FVector2d PhysicalFallbackCellSize = bRegional ? EvaluationScope.ReferenceDomain.GetCellSize() : DomainCellSize;
	const FVector2d PhysicalCellSizeMeters = PhysicalMetrics.ResolveSampleSpacingMeters(PhysicalResolution, PhysicalFallbackCellSize);
	const double PhysicalHeightScaleMeters = PhysicalMetrics.ResolveElevationScaleMeters(HeightScale);
	if (PhysicalCellSizeMeters.X <= UE_DOUBLE_SMALL_NUMBER || PhysicalCellSizeMeters.Y <= UE_DOUBLE_SMALL_NUMBER
		|| PhysicalHeightScaleMeters <= UE_DOUBLE_SMALL_NUMBER)
	{
		return Fail(TEXT("Terrain Context could not resolve valid physical terrain metrics."));
	}

	FEonformScalarField Elevation = MakeField(Height.Domain, EonformTerrainFieldNames::Elevation);
	FEonformScalarField Slope = MakeField(Height.Domain, EonformTerrainFieldNames::SlopeDegrees, EEonformFieldUnit::Degrees);
	FEonformScalarField Concavity = MakeField(Height.Domain, EonformTerrainFieldNames::Concavity);
	FEonformScalarField Convexity = MakeField(Height.Domain, EonformTerrainFieldNames::Convexity);
	FEonformScalarField Mountain;
	FEonformScalarField Foothill;
	FEonformScalarField Plains;
	const bool bHasMountain = CopyCompatibleField(InOutDataset, EonformTerrainFieldNames::Mountain, Height.Domain, Mountain);
	const bool bHasFoothill = CopyCompatibleField(InOutDataset, EonformTerrainFieldNames::Foothill, Height.Domain, Foothill);
	const bool bHasPlains = CopyCompatibleField(InOutDataset, EonformTerrainFieldNames::Plains, Height.Domain, Plains);
	if (!bHasMountain) Mountain = MakeField(Height.Domain, EonformTerrainFieldNames::Mountain);
	if (!bHasFoothill) Foothill = MakeField(Height.Domain, EonformTerrainFieldNames::Foothill);
	if (!bHasPlains) Plains = MakeField(Height.Domain, EonformTerrainFieldNames::Plains);

	float MinHeight = EvaluationScope.bUseGlobalHeightRange
		? EvaluationScope.GlobalMinimumHeight
		: TNumericLimits<float>::Max();
	float MaxHeight = EvaluationScope.bUseGlobalHeightRange
		? EvaluationScope.GlobalMaximumHeight
		: TNumericLimits<float>::Lowest();
	if (!EvaluationScope.bUseGlobalHeightRange)
	{
		for (const float Value : Height.Values)
		{
			MinHeight = FMath::Min(MinHeight, Value);
			MaxHeight = FMath::Max(MaxHeight, Value);
		}
	}
	const float HeightRange = FMath::Max(MaxHeight - MinHeight, UE_SMALL_NUMBER);
	const int32 Border = Height.Domain.BorderSamples;

	for (int32 Y = 0; Y < Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Dimensions.X; ++X)
		{
			const float Center = Height.AtInterior(X, Y);
			Elevation.AtInterior(X, Y) = FMath::Clamp((Center - MinHeight) / HeightRange, 0.0f, 1.0f);

			float Left = Center;
			float Right = Center;
			float Down = Center;
			float Up = Center;
			int32 HorizontalSpan = 0;
			int32 VerticalSpan = 0;
			FIntPoint ReferenceCoordinate(X, Y);

			if (bRegional)
			{
				ReferenceCoordinate = ResolveReferenceCoordinate(
					EvaluationScope.ReferenceDomain,
					Height.Domain.InteriorSampleToWorld(X, Y));
				const bool bHasLeft = ReferenceCoordinate.X > 0;
				const bool bHasRight = ReferenceCoordinate.X < EvaluationScope.ReferenceResolution.X - 1;
				const bool bHasDown = ReferenceCoordinate.Y > 0;
				const bool bHasUp = ReferenceCoordinate.Y < EvaluationScope.ReferenceResolution.Y - 1;
				if (bHasLeft) Left = Height.AtStorage(X + Border - 1, Y + Border);
				if (bHasRight) Right = Height.AtStorage(X + Border + 1, Y + Border);
				if (bHasDown) Down = Height.AtStorage(X + Border, Y + Border - 1);
				if (bHasUp) Up = Height.AtStorage(X + Border, Y + Border + 1);
				HorizontalSpan = static_cast<int32>(bHasLeft) + static_cast<int32>(bHasRight);
				VerticalSpan = static_cast<int32>(bHasDown) + static_cast<int32>(bHasUp);
			}
			else
			{
				const int32 XL = FMath::Max(0, X - 1);
				const int32 XR = FMath::Min(Dimensions.X - 1, X + 1);
				const int32 YD = FMath::Max(0, Y - 1);
				const int32 YU = FMath::Min(Dimensions.Y - 1, Y + 1);
				Left = Height.AtInterior(XL, Y);
				Right = Height.AtInterior(XR, Y);
				Down = Height.AtInterior(X, YD);
				Up = Height.AtInterior(X, YU);
				HorizontalSpan = XR - XL;
				VerticalSpan = YU - YD;
			}

			const double DX = static_cast<double>(Right - Left) * PhysicalHeightScaleMeters
				/ FMath::Max(static_cast<double>(HorizontalSpan) * PhysicalCellSizeMeters.X, UE_DOUBLE_SMALL_NUMBER);
			const double DY = static_cast<double>(Up - Down) * PhysicalHeightScaleMeters
				/ FMath::Max(static_cast<double>(VerticalSpan) * PhysicalCellSizeMeters.Y, UE_DOUBLE_SMALL_NUMBER);
			const double Gradient = FMath::Sqrt(DX * DX + DY * DY);
			Slope.AtInterior(X, Y) = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan(Gradient)));

			float NeighborSum = 0.0f;
			int32 NeighborCount = 0;
			for (int32 OY = -1; OY <= 1; ++OY)
			{
				for (int32 OX = -1; OX <= 1; ++OX)
				{
					if (OX == 0 && OY == 0) continue;
					if (bRegional)
					{
						const int32 RX = ReferenceCoordinate.X + OX;
						const int32 RY = ReferenceCoordinate.Y + OY;
						if (RX < 0 || RX >= EvaluationScope.ReferenceResolution.X
							|| RY < 0 || RY >= EvaluationScope.ReferenceResolution.Y) continue;
						NeighborSum += Height.AtStorage(X + Border + OX, Y + Border + OY);
						++NeighborCount;
					}
					else
					{
						const int32 NX = X + OX;
						const int32 NY = Y + OY;
						if (NX < 0 || NX >= Dimensions.X || NY < 0 || NY >= Dimensions.Y) continue;
						NeighborSum += Height.AtInterior(NX, NY);
						++NeighborCount;
					}
				}
			}

			if (NeighborCount > 0)
			{
				const float Delta = NeighborSum / static_cast<float>(NeighborCount) - Center;
				const double CurvatureScaleMeters = FMath::Max(FMath::Min(PhysicalCellSizeMeters.X, PhysicalCellSizeMeters.Y), UE_DOUBLE_SMALL_NUMBER);
				const float Curvature = FMath::Clamp(
					static_cast<float>(static_cast<double>(Delta) * PhysicalHeightScaleMeters / CurvatureScaleMeters),
					-1.0f,
					1.0f);
				Concavity.AtInterior(X, Y) = FMath::Max(Curvature, 0.0f);
				Convexity.AtInterior(X, Y) = FMath::Max(-Curvature, 0.0f);
			}

			const float MountainValue = bHasMountain
				? Mountain.AtInterior(X, Y)
				: SmoothStep01((Elevation.AtInterior(X, Y) - 0.58f) / 0.25f);
			const float FoothillValue = bHasFoothill
				? Foothill.AtInterior(X, Y)
				: FMath::Clamp(SmoothStep01((Elevation.AtInterior(X, Y) - 0.38f) / 0.22f) * (1.0f - MountainValue), 0.0f, 1.0f);
			const float PlainsValue = bHasPlains
				? Plains.AtInterior(X, Y)
				: FMath::Clamp(1.0f - MountainValue - FoothillValue * 0.65f, 0.0f, 1.0f);

			Mountain.AtInterior(X, Y) = FMath::Clamp(MountainValue, 0.0f, 1.0f);
			Foothill.AtInterior(X, Y) = FMath::Clamp(FoothillValue, 0.0f, 1.0f);
			Plains.AtInterior(X, Y) = FMath::Clamp(PlainsValue, 0.0f, 1.0f);
		}
	}

	if (!InOutDataset.SetScalarField(MoveTemp(Elevation))
		|| !InOutDataset.SetScalarField(MoveTemp(Slope))
		|| !InOutDataset.SetScalarField(MoveTemp(Concavity))
		|| !InOutDataset.SetScalarField(MoveTemp(Convexity))
		|| !InOutDataset.SetScalarField(MoveTemp(Mountain))
		|| !InOutDataset.SetScalarField(MoveTemp(Foothill))
		|| !InOutDataset.SetScalarField(MoveTemp(Plains)))
	{
		return Fail(TEXT("Terrain Context could not publish its derived fields."));
	}

	if (OutError) OutError->Reset();
	return true;
}

bool FEonformTerrainContext::BuildProcessMasks(
	const FEonformScalarField& Height,
	const FEonformTerrainProcessMaskSettings& Settings,
	FEonformTerrainDataset& InOutDataset,
	FString* OutError)
{
	auto Fail = [OutError](const TCHAR* Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (!Height.IsValid()) return Fail(TEXT("Process Masks requires a valid Height field."));

	const FEonformScalarField* Elevation = InOutDataset.FindScalarField(EonformTerrainFieldNames::Elevation);
	const FEonformScalarField* Slope = InOutDataset.FindScalarField(EonformTerrainFieldNames::SlopeDegrees);
	const FEonformScalarField* Concavity = InOutDataset.FindScalarField(EonformTerrainFieldNames::Concavity);
	const FEonformScalarField* Convexity = InOutDataset.FindScalarField(EonformTerrainFieldNames::Convexity);
	const FEonformScalarField* Mountain = InOutDataset.FindScalarField(EonformTerrainFieldNames::Mountain);
	const FEonformScalarField* Foothill = InOutDataset.FindScalarField(EonformTerrainFieldNames::Foothill);
	const FEonformScalarField* Plains = InOutDataset.FindScalarField(EonformTerrainFieldNames::Plains);

	const FEonformScalarField* Required[] = { Elevation, Slope, Concavity, Convexity, Mountain, Foothill, Plains };
	for (const FEonformScalarField* Field : Required)
	{
		if (!Field || !Field->IsValid() || Field->Domain != Height.Domain)
		{
			return Fail(TEXT("Process Masks requires Terrain Context fields on the same domain as Height."));
		}
	}

	const float ThermalRegionality = FMath::Clamp(Settings.ThermalRegionality, 0.0f, 1.0f);
	const float HydraulicRegionality = FMath::Clamp(Settings.HydraulicRegionality, 0.0f, 1.0f);
	const float RainfallHighlandBias = FMath::Clamp(Settings.RainfallHighlandBias, 0.0f, 1.0f);
	const float EvaporationLowlandBias = FMath::Clamp(Settings.EvaporationLowlandBias, 0.0f, 1.0f);
	const float TalusAngle = FMath::Clamp(Settings.ThermalTalusAngleDegrees, 0.0f, 90.0f);

	FEonformScalarField Thermal = MakeField(Height.Domain, EonformTerrainFieldNames::Thermal);
	FEonformScalarField Rainfall = MakeField(Height.Domain, EonformTerrainFieldNames::Rainfall);
	FEonformScalarField Hydraulic = MakeField(Height.Domain, EonformTerrainFieldNames::HydraulicErosion);
	FEonformScalarField Deposition = MakeField(Height.Domain, EonformTerrainFieldNames::Deposition);
	FEonformScalarField Evaporation = MakeField(Height.Domain, EonformTerrainFieldNames::Evaporation);

	for (int32 Y = 0; Y < Height.Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Height.Domain.Dimensions.X; ++X)
		{
			const float SlopeValue = Slope->AtInterior(X, Y);
			const float ElevationValue = Elevation->AtInterior(X, Y);
			const float MountainValue = Mountain->AtInterior(X, Y);
			const float FoothillValue = Foothill->AtInterior(X, Y);
			const float PlainsValue = Plains->AtInterior(X, Y);
			const float ConcavityValue = Concavity->AtInterior(X, Y);
			const float ConvexityValue = Convexity->AtInterior(X, Y);

			const float AboveTalus = SmoothStep01((SlopeValue - TalusAngle + 4.0f) / 12.0f);
			const float Highland = FMath::Clamp(MountainValue + FoothillValue * 0.65f, 0.0f, 1.0f);
			const float ThermalNatural = AboveTalus * FMath::Clamp(Highland + ConvexityValue * 0.35f, 0.0f, 1.0f);
			Thermal.AtInterior(X, Y) = FMath::Lerp(1.0f, ThermalNatural, ThermalRegionality);

			const float Orographic = FMath::Clamp(ElevationValue * RainfallHighlandBias + Highland * 0.55f + FoothillValue * 0.15f, 0.0f, 1.0f);
			const float RainNatural = FMath::Clamp(0.18f + Orographic, 0.0f, 1.0f);
			Rainfall.AtInterior(X, Y) = FMath::Lerp(1.0f, RainNatural, HydraulicRegionality);

			const float SlopeErosion = SmoothStep01((SlopeValue - 4.0f) / 26.0f);
			const float ErosionNatural = FMath::Clamp(SlopeErosion * (0.35f + Highland * 0.65f) * (1.0f - ConcavityValue * 0.45f), 0.0f, 1.0f);
			Hydraulic.AtInterior(X, Y) = FMath::Lerp(1.0f, ErosionNatural, HydraulicRegionality);

			const float LowSlope = 1.0f - SmoothStep01(SlopeValue / 12.0f);
			const float DepositionNatural = FMath::Clamp(LowSlope * (0.35f + ConcavityValue * 0.65f) * (0.45f + PlainsValue * 0.55f), 0.0f, 1.0f);
			Deposition.AtInterior(X, Y) = FMath::Lerp(1.0f, DepositionNatural, HydraulicRegionality);

			const float Lowland = 1.0f - ElevationValue;
			const float EvaporationNatural = FMath::Clamp(0.25f + Lowland * EvaporationLowlandBias + PlainsValue * 0.25f - ConcavityValue * 0.25f, 0.0f, 1.0f);
			Evaporation.AtInterior(X, Y) = FMath::Lerp(1.0f, EvaporationNatural, HydraulicRegionality);
		}
	}

	if (!InOutDataset.SetScalarField(MoveTemp(Thermal))
		|| !InOutDataset.SetScalarField(MoveTemp(Rainfall))
		|| !InOutDataset.SetScalarField(MoveTemp(Hydraulic))
		|| !InOutDataset.SetScalarField(MoveTemp(Deposition))
		|| !InOutDataset.SetScalarField(MoveTemp(Evaporation)))
	{
		return Fail(TEXT("Process Masks could not publish its derived fields."));
	}

	if (OutError) OutError->Reset();
	return true;
}