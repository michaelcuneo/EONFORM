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
	FGaeaScalarField Mountain = MakeField(Height.Domain, GaeaTerrainFieldNames::Mountain);
	FGaeaScalarField Foothill = MakeField(Height.Domain, GaeaTerrainFieldNames::Foothill);
	FGaeaScalarField Plains = MakeField(Height.Domain, GaeaTerrainFieldNames::Plains);

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

			const float MountainValue = SmoothStep01((Elevation.AtInterior(X, Y) - 0.58f) / 0.25f);
			const float FoothillValue = FMath::Clamp(
				SmoothStep01((Elevation.AtInterior(X, Y) - 0.38f) / 0.22f) * (1.0f - MountainValue),
				0.0f,
				1.0f);
			const float PlainsValue = FMath::Clamp(1.0f - MountainValue - FoothillValue * 0.65f, 0.0f, 1.0f);

			Mountain.AtInterior(X, Y) = MountainValue;
			Foothill.AtInterior(X, Y) = FoothillValue;
			Plains.AtInterior(X, Y) = PlainsValue;
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
