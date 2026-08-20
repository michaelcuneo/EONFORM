#include "GaeaThermalErosion.h"

namespace
{
	float ThermalMaskValue(const TArray<float>* Mask, int32 Index, int32 ExpectedNum)
	{
		if (!Mask || Mask->Num() != ExpectedNum)
		{
			return 1.0f;
		}
		return FMath::Clamp((*Mask)[Index], 0.0f, 1.0f);
	}

	float ThermalHardnessResistance(const TArray<float>* RockHardness, int32 Index, int32 ExpectedNum)
	{
		const float Hardness = RockHardness && RockHardness->Num() == ExpectedNum
			? FMath::Clamp((*RockHardness)[Index], 0.0f, 1.0f)
			: 0.5f;
		return FMath::Lerp(1.0f, 0.18f, Hardness);
	}
}

bool FGaeaThermalErosion::ApplyInPlace(
	FGaeaScalarField& HeightField,
	float HeightScale,
	const FGaeaThermalErosionSettings& Settings,
	const FGaeaScalarField* ProcessMask,
	const FGaeaScalarField* RockHardness,
	const FGaeaScalarField* AreaMask,
	FString* OutError)
{
	const FGaeaGridDomain Domain = HeightField.Domain;
	auto ValidateOptional = [&Domain, OutError](const FGaeaScalarField* Field, const TCHAR* Name)
	{
		if (!Field) return true;
		if (!Field->IsValid() || Field->Domain != Domain)
		{
			if (OutError) *OutError = FString::Printf(TEXT("Thermal Erosion input '%s' must use the same valid domain as Height."), Name);
			return false;
		}
		return true;
	};

	if (!ValidateOptional(ProcessMask, TEXT("Process Mask"))
		|| !ValidateOptional(RockHardness, TEXT("Rock Hardness"))
		|| !ValidateOptional(AreaMask, TEXT("Area Mask")))
	{
		return false;
	}

	TArray<float> CombinedMask;
	const TArray<float>* ProcessValues = ProcessMask ? &ProcessMask->Values : nullptr;
	if (AreaMask)
	{
		CombinedMask.SetNumUninitialized(HeightField.Values.Num());
		for (int32 Index = 0; Index < CombinedMask.Num(); ++Index)
		{
			const float Process = ProcessValues ? FMath::Clamp((*ProcessValues)[Index], 0.0f, 1.0f) : 1.0f;
			CombinedMask[Index] = Process * FMath::Clamp(AreaMask->Values[Index], 0.0f, 1.0f);
		}
		ProcessValues = &CombinedMask;
	}

	return ApplyInPlaceWithArrays(
		HeightField,
		HeightScale,
		Settings,
		ProcessValues,
		RockHardness ? &RockHardness->Values : nullptr,
		OutError);
}

bool FGaeaThermalErosion::ApplyInPlaceWithArrays(
	FGaeaScalarField& HeightField,
	float HeightScale,
	const FGaeaThermalErosionSettings& Settings,
	const TArray<float>* ProcessMask,
	const TArray<float>* RockHardness,
	FString* OutError)
{
	auto Fail = [OutError](const TCHAR* Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (!HeightField.IsValid()) return Fail(TEXT("Thermal Erosion requires a valid Height field."));
	if (HeightScale <= UE_SMALL_NUMBER) return Fail(TEXT("Thermal Erosion requires a positive HeightScale."));

	const FIntPoint Dimensions = HeightField.Domain.Dimensions;
	const FVector2d CellSize = HeightField.Domain.GetCellSize();
	if (Dimensions.X < 2 || Dimensions.Y < 2 || CellSize.X <= UE_SMALL_NUMBER || CellSize.Y <= UE_SMALL_NUMBER)
	{
		return Fail(TEXT("Thermal Erosion received an invalid Height domain."));
	}

	const int32 NumCells = HeightField.Values.Num();
	if ((ProcessMask && ProcessMask->Num() != NumCells) || (RockHardness && RockHardness->Num() != NumCells))
	{
		return Fail(TEXT("Thermal Erosion masks must match the Height sample count."));
	}

	if (Settings.Iterations <= 0 || Settings.Strength <= 0.0f)
	{
		if (OutError) OutError->Reset();
		return true;
	}

	const float SafeStrength = FMath::Clamp(Settings.Strength, 0.0f, 1.0f);
	const float SafeTalus = FMath::Clamp(Settings.TalusAngleDegrees, 0.0f, 89.9f);
	const double BaseCellSize = FMath::Max(FMath::Min(CellSize.X, CellSize.Y), static_cast<double>(UE_SMALL_NUMBER));
	const float TalusHeight = FMath::Tan(FMath::DegreesToRadians(SafeTalus)) * static_cast<float>(BaseCellSize) / HeightScale;

	TArray<float> Delta;
	Delta.SetNumZeroed(NumCells);

	static const FIntPoint Neighbors[] =
	{
		FIntPoint(-1, 0), FIntPoint(1, 0),
		FIntPoint(0, -1), FIntPoint(0, 1),
		FIntPoint(-1, -1), FIntPoint(1, -1),
		FIntPoint(-1, 1), FIntPoint(1, 1)
	};

	for (int32 Iteration = 0; Iteration < Settings.Iterations; ++Iteration)
	{
		FMemory::Memzero(Delta.GetData(), Delta.Num() * sizeof(float));
		for (int32 Y = 1; Y < Dimensions.Y - 1; ++Y)
		{
			for (int32 X = 1; X < Dimensions.X - 1; ++X)
			{
				const int32 CenterIndex = HeightField.Domain.GetStorageIndex(X + HeightField.Domain.BorderSamples, Y + HeightField.Domain.BorderSamples);
				const float LocalMask = ThermalMaskValue(ProcessMask, CenterIndex, NumCells);
				if (LocalMask <= UE_SMALL_NUMBER) continue;

				const float CenterHeight = HeightField.AtInterior(X, Y);
				float TotalExcess = 0.0f;
				float ExcessByNeighbor[UE_ARRAY_COUNT(Neighbors)] = {};
				for (int32 NeighborIndex = 0; NeighborIndex < UE_ARRAY_COUNT(Neighbors); ++NeighborIndex)
				{
					const FIntPoint Offset = Neighbors[NeighborIndex];
					const float NeighborHeight = HeightField.AtInterior(X + Offset.X, Y + Offset.Y);
					const float DistanceScale = (Offset.X != 0 && Offset.Y != 0) ? UE_SQRT_2 : 1.0f;
					const float Excess = CenterHeight - NeighborHeight - TalusHeight * DistanceScale;
					if (Excess > 0.0f)
					{
						ExcessByNeighbor[NeighborIndex] = Excess;
						TotalExcess += Excess;
					}
				}
				if (TotalExcess <= UE_SMALL_NUMBER) continue;

				const float Resistance = ThermalHardnessResistance(RockHardness, CenterIndex, NumCells);
				const float MaterialToMove = TotalExcess * 0.5f * SafeStrength * LocalMask * Resistance;
				Delta[CenterIndex] -= MaterialToMove;
				for (int32 NeighborIndex = 0; NeighborIndex < UE_ARRAY_COUNT(Neighbors); ++NeighborIndex)
				{
					const float Excess = ExcessByNeighbor[NeighborIndex];
					if (Excess <= 0.0f) continue;
					const FIntPoint Offset = Neighbors[NeighborIndex];
					const int32 NeighborStorageX = X + Offset.X + HeightField.Domain.BorderSamples;
					const int32 NeighborStorageY = Y + Offset.Y + HeightField.Domain.BorderSamples;
					const int32 NeighborValueIndex = HeightField.Domain.GetStorageIndex(NeighborStorageX, NeighborStorageY);
					Delta[NeighborValueIndex] += MaterialToMove * (Excess / TotalExcess);
				}
			}
		}
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			HeightField.Values[Index] += Delta[Index];
		}
	}

	if (OutError) OutError->Reset();
	return true;
}
