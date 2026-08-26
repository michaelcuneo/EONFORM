#include "EonformThermalErosion.h"

namespace
{
	float ThermalMaskValue(const TArray<float>* Mask, int32 Index, int32 ExpectedNum)
	{
		if (!Mask || Mask->Num() != ExpectedNum) return 1.0f;
		return FMath::Clamp((*Mask)[Index], 0.0f, 1.0f);
	}

	float ThermalHardnessResistance(const TArray<float>* RockHardness, int32 Index, int32 ExpectedNum)
	{
		const float Hardness = RockHardness && RockHardness->Num() == ExpectedNum
			? FMath::Clamp((*RockHardness)[Index], 0.0f, 1.0f)
			: 0.5f;
		return FMath::Lerp(1.0f, 0.18f, Hardness);
	}

	float ThermalHash01(int32 Value)
	{
		uint32 H = static_cast<uint32>(Value);
		H ^= H >> 16;
		H *= 0x7feb352dU;
		H ^= H >> 15;
		H *= 0x846ca68bU;
		H ^= H >> 16;
		return static_cast<float>(H & 0x00ffffffU) / 16777215.0f;
	}
}

bool FEonformThermalErosion::ApplyInPlace(
	FEonformScalarField& HeightField,
	float HeightScale,
	const FEonformThermalErosionSettings& Settings,
	const FEonformScalarField* ProcessMask,
	const FEonformScalarField* RockHardness,
	const FEonformScalarField* AreaMask,
	FString* OutError)
{
	const FEonformGridDomain Domain = HeightField.Domain;
	auto ValidateOptional = [&Domain, OutError](const FEonformScalarField* Field, const TCHAR* Name)
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

bool FEonformThermalErosion::ApplyInPlaceWithArrays(
	FEonformScalarField& HeightField,
	float HeightScale,
	const FEonformThermalErosionSettings& Settings,
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
	const float SafeAnisotropy = FMath::Clamp(Settings.Anisotropy, 0.0f, 1.0f);
	const float SafeSettling = FMath::Clamp(Settings.Settling, 0.0f, 1.0f);
	const float SafeRemoval = FMath::Clamp(Settings.SedimentRemoval, 0.0f, 1.0f);
	const int32 MaximumUsefulRadius = FMath::Max(1, FMath::Min(Dimensions.X, Dimensions.Y) / 8);
	const int32 MaxRadius = FMath::Clamp(FMath::RoundToInt(Settings.FeatureScaleSamples), 1, FMath::Min(MaximumUsefulRadius, 96));
	const double BaseCellSize = FMath::Max(FMath::Min(CellSize.X, CellSize.Y), static_cast<double>(UE_SMALL_NUMBER));
	const float TalusHeightPerCell = FMath::Tan(FMath::DegreesToRadians(SafeTalus)) * static_cast<float>(BaseCellSize) / HeightScale;

	TArray<float> Delta;
	Delta.SetNumZeroed(NumCells);

	static const FIntPoint Directions[] =
	{
		FIntPoint(-1, 0), FIntPoint(1, 0),
		FIntPoint(0, -1), FIntPoint(0, 1),
		FIntPoint(-1, -1), FIntPoint(1, -1),
		FIntPoint(-1, 1), FIntPoint(1, 1)
	};

	for (int32 Iteration = 0; Iteration < Settings.Iterations; ++Iteration)
	{
		FMemory::Memzero(Delta.GetData(), Delta.Num() * sizeof(float));

		// Cycle logarithmically through local and broad thermal scales. A physical
		// feature scale therefore changes morphology instead of merely multiplying
		// the strength of the same one-cell operation.
		const int32 ScalePhase = Iteration % 8;
		const float Phase01 = static_cast<float>(ScalePhase) / 7.0f;
		const int32 Radius = MaxRadius <= 1
			? 1
			: FMath::Clamp(FMath::RoundToInt(FMath::Pow(static_cast<float>(MaxRadius), Phase01)), 1, MaxRadius);
		const float AxisAngle = ThermalHash01(Settings.Seed + Iteration * 7919) * 2.0f * PI;
		const FVector2D PreferredAxis(FMath::Cos(AxisAngle), FMath::Sin(AxisAngle));

		for (int32 Y = 1; Y < Dimensions.Y - 1; ++Y)
		{
			for (int32 X = 1; X < Dimensions.X - 1; ++X)
			{
				const int32 CenterIndex = HeightField.Domain.GetStorageIndex(X + HeightField.Domain.BorderSamples, Y + HeightField.Domain.BorderSamples);
				const float LocalMask = ThermalMaskValue(ProcessMask, CenterIndex, NumCells);
				if (LocalMask <= UE_SMALL_NUMBER) continue;

				const float CenterHeight = HeightField.AtInterior(X, Y);
				float TotalWeightedExcess = 0.0f;
				float WeightedExcess[UE_ARRAY_COUNT(Directions)] = {};
				int32 NeighborX[UE_ARRAY_COUNT(Directions)] = {};
				int32 NeighborY[UE_ARRAY_COUNT(Directions)] = {};

				for (int32 NeighborIndex = 0; NeighborIndex < UE_ARRAY_COUNT(Directions); ++NeighborIndex)
				{
					const FIntPoint Direction = Directions[NeighborIndex];
					const int32 NX = X + Direction.X * Radius;
					const int32 NY = Y + Direction.Y * Radius;
					NeighborX[NeighborIndex] = NX;
					NeighborY[NeighborIndex] = NY;
					if (NX < 0 || NX >= Dimensions.X || NY < 0 || NY >= Dimensions.Y) continue;

					const float DistanceCells = static_cast<float>(Radius) * ((Direction.X != 0 && Direction.Y != 0) ? UE_SQRT_2 : 1.0f);
					const float NeighborHeight = HeightField.AtInterior(NX, NY);
					const float Excess = CenterHeight - NeighborHeight - TalusHeightPerCell * DistanceCells;
					if (Excess <= 0.0f) continue;

					const FVector2D DirectionUnit = FVector2D(static_cast<float>(Direction.X), static_cast<float>(Direction.Y)).GetSafeNormal();
					const float AxisAlignment = FMath::Abs(FVector2D::DotProduct(DirectionUnit, PreferredAxis));
					const float DirectionWeight = FMath::Lerp(1.0f, FMath::Lerp(0.22f, 1.0f, AxisAlignment), SafeAnisotropy);
					WeightedExcess[NeighborIndex] = Excess * DirectionWeight;
					TotalWeightedExcess += WeightedExcess[NeighborIndex];
				}
				if (TotalWeightedExcess <= UE_SMALL_NUMBER) continue;

				const float Resistance = ThermalHardnessResistance(RockHardness, CenterIndex, NumCells);
				const float ScaleDamping = 1.0f / FMath::Sqrt(static_cast<float>(Radius));
				const float MaterialToMove = FMath::Min(
					TotalWeightedExcess * 0.5f * SafeStrength * LocalMask * Resistance * ScaleDamping,
					FMath::Max(CenterHeight + 1.0f, 0.0f) * 0.25f);
				if (MaterialToMove <= UE_SMALL_NUMBER) continue;

				Delta[CenterIndex] -= MaterialToMove;
				const float MaterialToSettle = MaterialToMove * (1.0f - SafeRemoval) * FMath::Lerp(0.35f, 1.0f, SafeSettling);
				for (int32 NeighborIndex = 0; NeighborIndex < UE_ARRAY_COUNT(Directions); ++NeighborIndex)
				{
					const float Excess = WeightedExcess[NeighborIndex];
					if (Excess <= 0.0f) continue;
					const int32 NX = NeighborX[NeighborIndex];
					const int32 NY = NeighborY[NeighborIndex];
					if (NX < 0 || NX >= Dimensions.X || NY < 0 || NY >= Dimensions.Y) continue;
					const int32 NeighborValueIndex = HeightField.Domain.GetStorageIndex(NX + HeightField.Domain.BorderSamples, NY + HeightField.Domain.BorderSamples);
					Delta[NeighborValueIndex] += MaterialToSettle * (Excess / TotalWeightedExcess);
				}
			}
		}

		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			HeightField.Values[Index] = FMath::Clamp(HeightField.Values[Index] + Delta[Index], -1.0f, 1.0f);
		}
	}

	if (OutError) OutError->Reset();
	return true;
}
