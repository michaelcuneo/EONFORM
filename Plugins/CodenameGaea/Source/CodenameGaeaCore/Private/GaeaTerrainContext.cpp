#include "GaeaTerrainContext.h"

#include "GaeaTerrainFieldNames.h"

namespace
{
	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	FGaeaScalarField MakeField(const FGaeaGridDomain& Domain, FName Name, EGaeaFieldUnit Unit = EGaeaFieldUnit::Normalized)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;

		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor);
		return Field;
	}

	bool CopyCompatibleField(
		const FGaeaTerrainDataset& Dataset,
		FName Name,
		const FGaeaGridDomain& Domain,
		FGaeaScalarField& OutField)
	{
		const FGaeaScalarField* Existing = Dataset.FindScalarField(Name);
		if (!Existing || !Existing->IsValid() || Existing->Domain != Domain)
		{
			return false;
		}
		OutField = *Existing;
		return true;
	}
}

bool FGaeaTerrainContext::Analyze(
	const FGaeaScalarField& Height,
	float HeightScale,
	FGaeaTerrainDataset& InOutDataset,
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
	const FVector2d CellSize = Height.Domain.GetCellSize();
	if (Dimensions.X < 2 || Dimensions.Y < 2 || CellSize.X <= UE_SMALL_NUMBER || CellSize.Y <= UE_SMALL_NUMBER)
	{
		return Fail(TEXT("Terrain Context received an invalid Height domain."));
	}

	FGaeaScalarField Elevation = MakeField(Height.Domain, GaeaTerrainFieldNames::Elevation);
	FGaeaScalarField Slope = MakeField(Height.Domain, GaeaTerrainFieldNames::SlopeDegrees, EGaeaFieldUnit::Degrees);
	FGaeaScalarField Concavity = MakeField(Height.Domain, GaeaTerrainFieldNames::Concavity);
	FGaeaScalarField Convexity = MakeField(Height.Domain, GaeaTerrainFieldNames::Convexity);
	FGaeaScalarField Mountain;
	FGaeaScalarField Foothill;
	FGaeaScalarField Plains;
	const bool bHasMountain = CopyCompatibleField(InOutDataset, GaeaTerrainFieldNames::Mountain, Height.Domain, Mountain);
	const bool bHasFoothill = CopyCompatibleField(InOutDataset, GaeaTerrainFieldNames::Foothill, Height.Domain, Foothill);
	const bool bHasPlains = CopyCompatibleField(InOutDataset, GaeaTerrainFieldNames::Plains, Height.Domain, Plains);
	if (!bHasMountain) Mountain = MakeField(Height.Domain, GaeaTerrainFieldNames::Mountain);
	if (!bHasFoothill) Foothill = MakeField(Height.Domain, GaeaTerrainFieldNames::Foothill);
	if (!bHasPlains) Plains = MakeField(Height.Domain, GaeaTerrainFieldNames::Plains);

	float MinHeight = TNumericLimits<float>::Max();
	float MaxHeight = TNumericLimits<float>::Lowest();
	for (const float Value : Height.Values)
	{
		MinHeight = FMath::Min(MinHeight, Value);
		MaxHeight = FMath::Max(MaxHeight, Value);
	}
	const float HeightRange = FMath::Max(MaxHeight - MinHeight, UE_SMALL_NUMBER);

	for (int32 Y = 0; Y < Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Dimensions.X; ++X)
		{
			const float Center = Height.AtInterior(X, Y);
			Elevation.AtInterior(X, Y) = FMath::Clamp((Center - MinHeight) / HeightRange, 0.0f, 1.0f);

			const int32 XL = FMath::Max(0, X - 1);
			const int32 XR = FMath::Min(Dimensions.X - 1, X + 1);
			const int32 YD = FMath::Max(0, Y - 1);
			const int32 YU = FMath::Min(Dimensions.Y - 1, Y + 1);

			const float DX = (Height.AtInterior(XR, Y) - Height.AtInterior(XL, Y)) * HeightScale
				/ FMath::Max(static_cast<float>(XR - XL) * static_cast<float>(CellSize.X), UE_SMALL_NUMBER);
			const float DY = (Height.AtInterior(X, YU) - Height.AtInterior(X, YD)) * HeightScale
				/ FMath::Max(static_cast<float>(YU - YD) * static_cast<float>(CellSize.Y), UE_SMALL_NUMBER);
			const float Gradient = FMath::Sqrt(DX * DX + DY * DY);
			Slope.AtInterior(X, Y) = FMath::RadiansToDegrees(FMath::Atan(Gradient));

			float NeighborSum = 0.0f;
			int32 NeighborCount = 0;
			for (int32 OY = -1; OY <= 1; ++OY)
			{
				for (int32 OX = -1; OX <= 1; ++OX)
				{
					if (OX == 0 && OY == 0) continue;
					const int32 NX = X + OX;
					const int32 NY = Y + OY;
					if (NX < 0 || NX >= Dimensions.X || NY < 0 || NY >= Dimensions.Y) continue;
					NeighborSum += Height.AtInterior(NX, NY);
					++NeighborCount;
				}
			}

			if (NeighborCount > 0)
			{
				const float Delta = NeighborSum / static_cast<float>(NeighborCount) - Center;
				const float CurvatureScale = FMath::Max(static_cast<float>(FMath::Min(CellSize.X, CellSize.Y)), UE_SMALL_NUMBER);
				const float Curvature = FMath::Clamp(Delta * HeightScale / CurvatureScale, -1.0f, 1.0f);
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

bool FGaeaTerrainContext::BuildProcessMasks(
	const FGaeaScalarField& Height,
	const FGaeaTerrainProcessMaskSettings& Settings,
	FGaeaTerrainDataset& InOutDataset,
	FString* OutError)
{
	auto Fail = [OutError](const TCHAR* Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (!Height.IsValid()) return Fail(TEXT("Process Masks requires a valid Height field."));

	const FGaeaScalarField* Elevation = InOutDataset.FindScalarField(GaeaTerrainFieldNames::Elevation);
	const FGaeaScalarField* Slope = InOutDataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
	const FGaeaScalarField* Concavity = InOutDataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
	const FGaeaScalarField* Convexity = InOutDataset.FindScalarField(GaeaTerrainFieldNames::Convexity);
	const FGaeaScalarField* Mountain = InOutDataset.FindScalarField(GaeaTerrainFieldNames::Mountain);
	const FGaeaScalarField* Foothill = InOutDataset.FindScalarField(GaeaTerrainFieldNames::Foothill);
	const FGaeaScalarField* Plains = InOutDataset.FindScalarField(GaeaTerrainFieldNames::Plains);

	const FGaeaScalarField* Required[] = { Elevation, Slope, Concavity, Convexity, Mountain, Foothill, Plains };
	for (const FGaeaScalarField* Field : Required)
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

	FGaeaScalarField Thermal = MakeField(Height.Domain, GaeaTerrainFieldNames::Thermal);
	FGaeaScalarField Rainfall = MakeField(Height.Domain, GaeaTerrainFieldNames::Rainfall);
	FGaeaScalarField Hydraulic = MakeField(Height.Domain, GaeaTerrainFieldNames::HydraulicErosion);
	FGaeaScalarField Deposition = MakeField(Height.Domain, GaeaTerrainFieldNames::Deposition);
	FGaeaScalarField Evaporation = MakeField(Height.Domain, GaeaTerrainFieldNames::Evaporation);

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
