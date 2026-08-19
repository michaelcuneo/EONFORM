#include "GaeaHydraulicErosion.h"

#include "GaeaTerrainFieldNames.h"

namespace
{
	float MaskValue(const TArray<float>* Mask, int32 Index, int32 ExpectedNum)
	{
		if (!Mask || Mask->Num() != ExpectedNum)
		{
			return 1.0f;
		}
		return FMath::Clamp((*Mask)[Index], 0.0f, 1.0f);
	}

	float HardnessResistance(const TArray<float>* RockHardness, int32 Index, int32 ExpectedNum, float RockSoftness)
	{
		const float Hardness = RockHardness && RockHardness->Num() == ExpectedNum
			? FMath::Clamp((*RockHardness)[Index], 0.0f, 1.0f)
			: 0.5f;
		const float EffectiveHardness = Hardness * (1.0f - FMath::Clamp(RockSoftness, 0.0f, 1.0f));
		return FMath::Lerp(1.0f, 0.18f, EffectiveHardness);
	}

	const TArray<float>* ValuesIfCompatible(const FGaeaScalarField* Field, const FGaeaGridDomain& Domain)
	{
		return Field && Field->IsValid() && Field->Domain == Domain ? &Field->Values : nullptr;
	}

	FGaeaScalarField MakeField(const FGaeaGridDomain& Domain, FName Name, TArray<float>&& Values)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor);
		if (Field.Values.Num() == Values.Num())
		{
			Field.Values = MoveTemp(Values);
		}
		return Field;
	}
}

bool FGaeaHydraulicErosionResult::IsValid() const
{
	return Height.IsValid() && Wear.IsValid() && Deposits.IsValid() && Flow.IsValid()
		&& Wear.Domain == Height.Domain && Deposits.Domain == Height.Domain && Flow.Domain == Height.Domain;
}

bool FGaeaHydraulicErosion::ApplyInPlace(
	FGaeaScalarField& HeightField,
	float HeightScale,
	const FGaeaHydraulicErosionSettings& Settings,
	TArray<float>* OutFlowAccumulation,
	const TArray<float>* RainfallMask,
	const TArray<float>* ErosionMask,
	const TArray<float>* DepositionMask,
	const TArray<float>* EvaporationMask,
	const TArray<float>* RockHardness,
	const TArray<float>* SoilDepth,
	TArray<float>* OutWear,
	TArray<float>* OutDeposits)
{
	if (!HeightField.IsValid() || Settings.Iterations <= 0 || HeightScale <= UE_SMALL_NUMBER || HeightField.Domain.BorderSamples != 0)
	{
		if (OutFlowAccumulation) OutFlowAccumulation->Reset();
		if (OutWear) OutWear->Reset();
		if (OutDeposits) OutDeposits->Reset();
		return false;
	}

	const int32 ResolutionX = HeightField.Domain.Dimensions.X;
	const int32 ResolutionY = HeightField.Domain.Dimensions.Y;
	const int32 NumCells = HeightField.Values.Num();
	const FVector2d CellSize = HeightField.Domain.GetCellSize();
	const float RepresentativeCellSize = static_cast<float>(FMath::Max(FMath::Min(CellSize.X, CellSize.Y), UE_SMALL_NUMBER));
	const float HeightToSlope = HeightScale / RepresentativeCellSize;

	const float Rainfall = FMath::Max(Settings.Rainfall, 0.0f);
	const float FlowRate = FMath::Clamp(Settings.FlowRate, 0.0f, 1.0f);
	const float CapacityFactor = FMath::Max(Settings.SedimentCapacity, 0.0f);
	const float ErosionRate = FMath::Clamp(Settings.ErosionRate * FMath::Max(Settings.Strength, 0.0f), 0.0f, 4.0f);
	const float DepositionRate = FMath::Clamp(Settings.DepositionRate, 0.0f, 1.0f);
	const float Evaporation = FMath::Clamp(Settings.Evaporation, 0.0f, 1.0f);
	const float MinimumSlope = FMath::Max(Settings.MinimumSlope, 0.0f);
	const float RockSoftness = FMath::Clamp(Settings.RockSoftness, 0.0f, 1.0f);

	TArray<float> Water, Sediment, NextWater, NextSediment, Flow, Wear, Deposits;
	Water.SetNumZeroed(NumCells); Sediment.SetNumZeroed(NumCells);
	NextWater.SetNumZeroed(NumCells); NextSediment.SetNumZeroed(NumCells);
	Flow.SetNumZeroed(NumCells); Wear.SetNumZeroed(NumCells); Deposits.SetNumZeroed(NumCells);

	static const FIntPoint Neighbors[] = {
		FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(0, 1),
		FIntPoint(-1, -1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(1, 1)
	};

	auto Index = [ResolutionX](int32 X, int32 Y) { return Y * ResolutionX + X; };

	for (int32 Iteration = 0; Iteration < Settings.Iterations; ++Iteration)
	{
		for (int32 I = 0; I < NumCells; ++I)
		{
			Water[I] += Rainfall * MaskValue(RainfallMask, I, NumCells);
		}
		FMemory::Memzero(NextWater.GetData(), NumCells * sizeof(float));
		FMemory::Memzero(NextSediment.GetData(), NumCells * sizeof(float));

		for (int32 Y = 0; Y < ResolutionY; ++Y)
		{
			for (int32 X = 0; X < ResolutionX; ++X)
			{
				const int32 I = Index(X, Y);
				const float AvailableWater = Water[I];
				const float AvailableSediment = Sediment[I];
				if (AvailableWater <= UE_SMALL_NUMBER)
				{
					NextSediment[I] += AvailableSediment;
					continue;
				}

				const float SurfaceHeight = HeightField.Values[I] + AvailableWater;
				float Drops[UE_ARRAY_COUNT(Neighbors)] = {};
				float TotalDrop = 0.0f;
				for (int32 N = 0; N < UE_ARRAY_COUNT(Neighbors); ++N)
				{
					const int32 NX = X + Neighbors[N].X;
					const int32 NY = Y + Neighbors[N].Y;
					if (NX < 0 || NX >= ResolutionX || NY < 0 || NY >= ResolutionY) continue;
					const int32 NI = Index(NX, NY);
					const float Drop = SurfaceHeight - (HeightField.Values[NI] + Water[NI]);
					if (Drop > 0.0f) { Drops[N] = Drop; TotalDrop += Drop; }
				}

				if (TotalDrop <= UE_SMALL_NUMBER || FlowRate <= UE_SMALL_NUMBER)
				{
					NextWater[I] += AvailableWater;
					NextSediment[I] += AvailableSediment;
					continue;
				}

				const float WaterToMove = AvailableWater * FlowRate;
				const float SedimentToMove = AvailableSediment * FlowRate;
				NextWater[I] += AvailableWater - WaterToMove;
				NextSediment[I] += AvailableSediment - SedimentToMove;

				for (int32 N = 0; N < UE_ARRAY_COUNT(Neighbors); ++N)
				{
					if (Drops[N] <= 0.0f) continue;
					const int32 NI = Index(X + Neighbors[N].X, Y + Neighbors[N].Y);
					const float Share = Drops[N] / TotalDrop;
					const float MovedWater = WaterToMove * Share;
					NextWater[NI] += MovedWater;
					NextSediment[NI] += SedimentToMove * Share;
					Flow[NI] += MovedWater;
				}
			}
		}

		Water = MoveTemp(NextWater); Sediment = MoveTemp(NextSediment);
		NextWater.SetNumZeroed(NumCells); NextSediment.SetNumZeroed(NumCells);

		for (int32 Y = 1; Y < ResolutionY - 1; ++Y)
		{
			for (int32 X = 1; X < ResolutionX - 1; ++X)
			{
				const int32 I = Index(X, Y);
				float MaxDownhillDrop = 0.0f;
				for (const FIntPoint& Offset : Neighbors)
				{
					const float DistanceScale = (Offset.X != 0 && Offset.Y != 0) ? UE_SQRT_2 : 1.0f;
					const float Drop = (HeightField.Values[I] - HeightField.Values[Index(X + Offset.X, Y + Offset.Y)]) / DistanceScale;
					MaxDownhillDrop = FMath::Max(MaxDownhillDrop, Drop);
				}
				const float PhysicalSlope = FMath::Max(MaxDownhillDrop * HeightToSlope, MinimumSlope);
				const float Capacity = Water[I] * PhysicalSlope * CapacityFactor;
				if (Sediment[I] > Capacity)
				{
					const float SoilRetention = SoilDepth && SoilDepth->Num() == NumCells
						? FMath::Lerp(1.0f, 1.45f, FMath::Clamp((*SoilDepth)[I], 0.0f, 1.0f)) : 1.0f;
					const float LocalDeposition = FMath::Clamp(DepositionRate * MaskValue(DepositionMask, I, NumCells) * SoilRetention, 0.0f, 1.0f);
					const float Deposit = (Sediment[I] - Capacity) * LocalDeposition;
					HeightField.Values[I] += Deposit; Sediment[I] -= Deposit; Deposits[I] += Deposit;
				}
				else if (Sediment[I] < Capacity)
				{
					const float Resistance = HardnessResistance(RockHardness, I, NumCells, RockSoftness);
					const float LocalErosion = ErosionRate * MaskValue(ErosionMask, I, NumCells) * Resistance;
					const float Erode = FMath::Min((Capacity - Sediment[I]) * LocalErosion, 0.02f);
					HeightField.Values[I] -= Erode; Sediment[I] += Erode; Wear[I] += Erode;
				}
			}
		}

		for (int32 I = 0; I < NumCells; ++I)
		{
			Water[I] *= 1.0f - FMath::Clamp(Evaporation * MaskValue(EvaporationMask, I, NumCells), 0.0f, 1.0f);
		}
	}

	if (OutFlowAccumulation) *OutFlowAccumulation = MoveTemp(Flow);
	if (OutWear) *OutWear = MoveTemp(Wear);
	if (OutDeposits) *OutDeposits = MoveTemp(Deposits);
	return true;
}

bool FGaeaHydraulicErosion::EvaluateWithArrays(
	const FGaeaScalarField& InputHeight,
	float HeightScale,
	const FGaeaHydraulicErosionSettings& Settings,
	FGaeaHydraulicErosionResult& OutResult,
	const TArray<float>* RainfallMask,
	const TArray<float>* ErosionMask,
	const TArray<float>* DepositionMask,
	const TArray<float>* EvaporationMask,
	const TArray<float>* RockHardness,
	const TArray<float>* SoilDepth)
{
	OutResult = FGaeaHydraulicErosionResult{};
	if (!InputHeight.IsValid()) return false;
	FGaeaScalarField Height = InputHeight;
	Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
	TArray<float> Flow, Wear, Deposits;
	if (!ApplyInPlace(Height, HeightScale, Settings, &Flow, RainfallMask, ErosionMask, DepositionMask, EvaporationMask, RockHardness, SoilDepth, &Wear, &Deposits)) return false;
	OutResult.Height = MoveTemp(Height);
	OutResult.Wear = MakeField(InputHeight.Domain, GaeaTerrainFieldNames::Wear, MoveTemp(Wear));
	OutResult.Deposits = MakeField(InputHeight.Domain, GaeaTerrainFieldNames::Deposits, MoveTemp(Deposits));
	OutResult.Flow = MakeField(InputHeight.Domain, GaeaTerrainFieldNames::Flow, MoveTemp(Flow));
	return OutResult.IsValid();
}

bool FGaeaHydraulicErosion::Evaluate(
	const FGaeaScalarField& InputHeight,
	float HeightScale,
	const FGaeaHydraulicErosionSettings& Settings,
	FGaeaHydraulicErosionResult& OutResult,
	const FGaeaScalarField* RainfallMask,
	const FGaeaScalarField* ErosionMask,
	const FGaeaScalarField* DepositionMask,
	const FGaeaScalarField* EvaporationMask,
	const FGaeaScalarField* RockHardness,
	const FGaeaScalarField* SoilDepth)
{
	return EvaluateWithArrays(InputHeight, HeightScale, Settings, OutResult,
		ValuesIfCompatible(RainfallMask, InputHeight.Domain),
		ValuesIfCompatible(ErosionMask, InputHeight.Domain),
		ValuesIfCompatible(DepositionMask, InputHeight.Domain),
		ValuesIfCompatible(EvaporationMask, InputHeight.Domain),
		ValuesIfCompatible(RockHardness, InputHeight.Domain),
		ValuesIfCompatible(SoilDepth, InputHeight.Domain));
}
