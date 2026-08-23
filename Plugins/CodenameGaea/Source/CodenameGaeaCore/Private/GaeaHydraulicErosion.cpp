#include "GaeaHydraulicErosion.h"

#include "GaeaTerrainFieldNames.h"
#include "HAL/PlatformTime.h"

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

	float SeedVariation(int32 RuntimeSeed, int32 Iteration, int32 Index)
	{
		uint32 Hash = GetTypeHash(RuntimeSeed);
		Hash = HashCombineFast(Hash, GetTypeHash(Iteration));
		Hash = HashCombineFast(Hash, GetTypeHash(Index));
		const float Unit = static_cast<float>(Hash & 0xffffu) / 65535.0f;
		return FMath::Lerp(0.9f, 1.1f, Unit);
	}

	bool ApplyAdvancedFlowErosion(
		FGaeaScalarField& HeightField,
		float HeightScale,
		const FGaeaHydraulicErosionSettings& Settings,
		TArray<float>* OutFlowAccumulation,
		const TArray<float>* RainfallMask,
		const TArray<float>* ErosionMask,
		const TArray<float>* DepositionMask,
		const TArray<float>* RockHardness,
		const TArray<float>* SoilDepth,
		TArray<float>* OutWear,
		TArray<float>* OutDeposits,
		const TArray<float>* AreaMask,
		const TArray<float>* InitialSediment)
	{
		const int32 Width = HeightField.Domain.Dimensions.X;
		const int32 Height = HeightField.Domain.Dimensions.Y;
		const int32 NumCells = HeightField.Values.Num();
		if (Width < 3 || Height < 3 || NumCells != Width * Height)
		{
			return false;
		}

		const FVector2d CellSize = HeightField.Domain.GetCellSize();
		const double DomainSpacing = FMath::Max(FMath::Min(FMath::Abs(CellSize.X), FMath::Abs(CellSize.Y)), UE_DOUBLE_SMALL_NUMBER);
		const double SampleSpacingMeters = Settings.PhysicalSampleSpacingMeters > UE_DOUBLE_SMALL_NUMBER
			? Settings.PhysicalSampleSpacingMeters
			: DomainSpacing;
		const double ElevationScaleMeters = Settings.PhysicalElevationScaleMeters > UE_DOUBLE_SMALL_NUMBER
			? Settings.PhysicalElevationScaleMeters
			: static_cast<double>(HeightScale);
		const double SlopeScale = ElevationScaleMeters / FMath::Max(SampleSpacingMeters, UE_DOUBLE_SMALL_NUMBER);

		const float FeatureScale = FMath::Clamp(Settings.FeatureScale, 0.25f, 8.0f);
		const float Strength = FMath::Max(Settings.Strength, 0.0f) * (Settings.bAggressiveMode ? 1.65f : 1.0f);
		const float Downcutting = FMath::Clamp(Settings.Downcutting, 0.0f, 2.0f);
		const float Debris = FMath::Clamp(Settings.Debris, 0.0f, 1.0f);
		const float CapacityFactor = FMath::Max(Settings.SedimentCapacity * FMath::Lerp(0.65f, 1.55f, Debris), 0.01f);
		const float ErosionRate = FMath::Clamp(Settings.ErosionRate * Strength, 0.0f, 8.0f);
		const float DepositionRate = FMath::Clamp(Settings.DepositionRate, 0.0f, 1.0f);
		const float SedimentRemoval = FMath::Clamp(Settings.SedimentRemoval, 0.0f, 1.0f);
		const float BaseLevel = FMath::Clamp(Settings.BaseLevel, -1.0f, 1.0f);
		const float BaseRockSoftness = FMath::Clamp(Settings.RockSoftness, 0.0f, 1.0f);
		const float Inhibition = FMath::Clamp(Settings.Inhibition, 0.0f, 1.0f);
		const float Volume = FMath::Clamp(Settings.Volume, 0.05f, 4.0f);
		const float Rainfall = FMath::Max(Settings.Rainfall * Volume, 0.00001f);
		const bool bSelectErosionStrength = Settings.SelectiveProcessing == TEXT("ErosionStrength");
		const bool bSelectRockSoftness = Settings.SelectiveProcessing == TEXT("RockSoftness");
		const bool bSelectPrecipitation = Settings.SelectiveProcessing == TEXT("Precipitation");
		const int32 RuntimeSeed = Settings.bDeterministic
			? Settings.Seed
			: Settings.Seed ^ static_cast<int32>(FPlatformTime::Cycles());

		TArray<int32> Receiver;
		TArray<uint8> DonorCount;
		TArray<int32> Queue;
		TArray<int32> Order;
		TArray<float> Runoff;
		TArray<float> Sediment;
		TArray<float> DeltaHeight;
		TArray<float> Flow;
		TArray<float> Wear;
		TArray<float> Deposits;
		Receiver.SetNumUninitialized(NumCells);
		DonorCount.SetNumZeroed(NumCells);
		Queue.Reserve(NumCells);
		Order.Reserve(NumCells);
		Runoff.SetNumZeroed(NumCells);
		Sediment.SetNumZeroed(NumCells);
		DeltaHeight.SetNumZeroed(NumCells);
		Flow.SetNumZeroed(NumCells);
		Wear.SetNumZeroed(NumCells);
		Deposits.SetNumZeroed(NumCells);

		if (InitialSediment && InitialSediment->Num() == NumCells)
		{
			for (int32 I = 0; I < NumCells; ++I)
			{
				Sediment[I] = FMath::Max((*InitialSediment)[I], 0.0f);
			}
		}

		static const FIntPoint Neighbors[] = {
			FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(0, 1),
			FIntPoint(-1, -1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(1, 1)
		};
		auto Index = [Width](int32 X, int32 Y) { return Y * Width + X; };

		for (int32 Iteration = 0; Iteration < Settings.Iterations; ++Iteration)
		{
			FMemory::Memset(Receiver.GetData(), 0xff, NumCells * sizeof(int32));
			FMemory::Memzero(DonorCount.GetData(), NumCells * sizeof(uint8));
			FMemory::Memzero(Runoff.GetData(), NumCells * sizeof(float));
			FMemory::Memzero(DeltaHeight.GetData(), NumCells * sizeof(float));
			Queue.Reset();
			Order.Reset();

			// Build a strictly downhill D8 receiver graph. Because every receiver is
			// lower than its donor, this graph is acyclic and can be accumulated in O(N).
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					const int32 I = Index(X, Y);
					const float Current = HeightField.Values[I];
					float BestGradient = 0.0f;
					int32 BestReceiver = -1;
					for (const FIntPoint& Offset : Neighbors)
					{
						const int32 NX = X + Offset.X;
						const int32 NY = Y + Offset.Y;
						if (NX < 0 || NX >= Width || NY < 0 || NY >= Height) continue;
						const int32 NI = Index(NX, NY);
						const float Drop = Current - HeightField.Values[NI];
						if (Drop <= 1.0e-7f) continue;
						const float Distance = Offset.X != 0 && Offset.Y != 0 ? UE_SQRT_2 : 1.0f;
						const float Gradient = Drop / Distance;
						if (Gradient > BestGradient)
						{
							BestGradient = Gradient;
							BestReceiver = NI;
						}
					}
					Receiver[I] = BestReceiver;
					if (BestReceiver >= 0 && DonorCount[BestReceiver] < MAX_uint8)
					{
						++DonorCount[BestReceiver];
					}

					const float Area = MaskValue(AreaMask, I, NumCells);
					const float SelectiveRain = bSelectPrecipitation ? Area : 1.0f;
					const float RainJitter = SeedVariation(RuntimeSeed, Iteration, I);
					Runoff[I] = Rainfall * MaskValue(RainfallMask, I, NumCells) * SelectiveRain * RainJitter;
				}
			}

			// Sources have no donors. Propagating them downstream yields contributing
			// runoff while also giving us a source-to-outlet processing order.
			for (int32 I = 0; I < NumCells; ++I)
			{
				if (DonorCount[I] == 0) Queue.Add(I);
			}
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const int32 I = Queue[Head];
				Order.Add(I);
				const int32 R = Receiver[I];
				if (R >= 0)
				{
					Runoff[R] += Runoff[I];
					if (DonorCount[R] > 0 && --DonorCount[R] == 0) Queue.Add(R);
				}
			}

			// The strict-lower receiver rule should make every component acyclic, but
			// keep isolated pathological samples deterministic rather than dropping them.
			if (Order.Num() != NumCells)
			{
				TBitArray<> Seen(false, NumCells);
				for (const int32 I : Order) Seen[I] = true;
				for (int32 I = 0; I < NumCells; ++I) if (!Seen[I]) Order.Add(I);
			}

			for (const int32 I : Order)
			{
				const int32 R = Receiver[I];
				if (R < 0) continue;

				const int32 X = I % Width;
				const int32 Y = I / Width;
				const int32 RX = R % Width;
				const int32 RY = R / Width;
				const float DistanceCells = (X != RX && Y != RY) ? UE_SQRT_2 : 1.0f;
				const float Drop = FMath::Max(HeightField.Values[I] - HeightField.Values[R], 0.0f);
				const float PhysicalSlope = FMath::Max(
					static_cast<float>(Drop * SlopeScale / DistanceCells / FeatureScale),
					Settings.MinimumSlope);

				const float LocalRain = FMath::Max(Rainfall, 1.0e-6f);
				const float ContributingCells = FMath::Max(Runoff[I] / LocalRain, 1.0f);
				const float FlowResponse = FMath::Clamp(FMath::Pow(FMath::Log2(1.0f + ContributingCells), 0.82f), 0.0f, 7.5f);
				const float SlopeResponse = FMath::Pow(FMath::Clamp(PhysicalSlope, 0.0f, 4.0f), 0.78f);
				const float StreamPower = FlowResponse * SlopeResponse;
				const float Capacity = StreamPower * CapacityFactor * 0.0028f * Volume;

				const float Area = MaskValue(AreaMask, I, NumCells);
				const float SelectiveStrength = bSelectErosionStrength ? Area : 1.0f;
				const float LocalSoftness = bSelectRockSoftness ? BaseRockSoftness * Area : BaseRockSoftness;
				const float Resistance = HardnessResistance(RockHardness, I, NumCells, LocalSoftness);
				const float InhibitionFactor = 1.0f - FMath::Clamp(Wear[I] * Inhibition * 7.0f, 0.0f, 0.92f);
				const float Variation = SeedVariation(RuntimeSeed + 7919, Iteration, I);
				const float ErosionEligibility = MaskValue(ErosionMask, I, NumCells) * SelectiveStrength;

				float LocalSediment = Sediment[I];
				if (LocalSediment < Capacity && HeightField.Values[I] > BaseLevel)
				{
					const float DowncuttingBoost = 1.0f + Downcutting * FMath::Clamp(FlowResponse / 5.0f, 0.0f, 1.0f);
					const float Potential = (Capacity - LocalSediment)
						* ErosionRate
						* Resistance
						* ErosionEligibility
						* InhibitionFactor
						* DowncuttingBoost
						* Variation;
					const float MaxIncision = FMath::Lerp(0.0012f, 0.0045f, FMath::Clamp(FlowResponse / 7.5f, 0.0f, 1.0f));
					const float Erode = FMath::Min3(Potential, MaxIncision, FMath::Max(HeightField.Values[I] - BaseLevel, 0.0f));
					if (Erode > 0.0f)
					{
						DeltaHeight[I] -= Erode;
						Wear[I] += Erode;
						LocalSediment += Erode;

						// Feature scale widens mature channels slightly instead of turning every
						// drainage line into a one-cell trench.
						const float BankShare = FMath::Clamp(0.025f + (FeatureScale - 0.5f) * 0.025f, 0.015f, 0.15f);
						if (BankShare > 0.0f && X > 0 && X + 1 < Width && Y > 0 && Y + 1 < Height)
						{
							const float BankCut = Erode * BankShare;
							DeltaHeight[Index(X - 1, Y)] -= BankCut * 0.35f;
							DeltaHeight[Index(X + 1, Y)] -= BankCut * 0.35f;
							DeltaHeight[Index(X, Y - 1)] -= BankCut * 0.35f;
							DeltaHeight[Index(X, Y + 1)] -= BankCut * 0.35f;
						}
					}
				}
				else if (LocalSediment > Capacity)
				{
					const float SoilRetention = SoilDepth && SoilDepth->Num() == NumCells
						? FMath::Lerp(0.85f, 1.35f, FMath::Clamp((*SoilDepth)[I], 0.0f, 1.0f))
						: 1.0f;
					const float Deposit = (LocalSediment - Capacity)
						* DepositionRate
						* MaskValue(DepositionMask, I, NumCells)
						* SoilRetention;
					DeltaHeight[I] += Deposit;
					Deposits[I] += Deposit;
					LocalSediment -= Deposit;
				}

				Sediment[I] = 0.0f;
				Sediment[R] += LocalSediment * (1.0f - SedimentRemoval);
				Flow[I] += FlowResponse;
			}

			for (int32 I = 0; I < NumCells; ++I)
			{
				const float NextHeight = HeightField.Values[I] + DeltaHeight[I];
				HeightField.Values[I] = FMath::Max(NextHeight, BaseLevel);
			}
		}

		if (OutFlowAccumulation) *OutFlowAccumulation = MoveTemp(Flow);
		if (OutWear) *OutWear = MoveTemp(Wear);
		if (OutDeposits) *OutDeposits = MoveTemp(Deposits);
		return true;
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
	TArray<float>* OutDeposits,
	const TArray<float>* AreaMask,
	const TArray<float>* InitialSediment)
{
	if (!HeightField.IsValid() || Settings.Iterations <= 0 || HeightScale <= UE_SMALL_NUMBER || HeightField.Domain.BorderSamples != 0)
	{
		if (OutFlowAccumulation) OutFlowAccumulation->Reset();
		if (OutWear) OutWear->Reset();
		if (OutDeposits) OutDeposits->Reset();
		return false;
	}

	if (Settings.bAdvancedFlowSolver)
	{
		return ApplyAdvancedFlowErosion(
			HeightField,
			HeightScale,
			Settings,
			OutFlowAccumulation,
			RainfallMask,
			ErosionMask,
			DepositionMask,
			RockHardness,
			SoilDepth,
			OutWear,
			OutDeposits,
			AreaMask,
			InitialSediment);
	}

	const int32 ResolutionX = HeightField.Domain.Dimensions.X;
	const int32 ResolutionY = HeightField.Domain.Dimensions.Y;
	const int32 NumCells = HeightField.Values.Num();
	const FVector2d CellSize = HeightField.Domain.GetCellSize();
	const float RepresentativeCellSize = static_cast<float>(FMath::Max(FMath::Min(CellSize.X, CellSize.Y), UE_SMALL_NUMBER));
	const float FeatureScale = FMath::Clamp(Settings.FeatureScale, 0.25f, 8.0f);
	const float HeightToSlope = HeightScale / (RepresentativeCellSize * FeatureScale);

	const float AggressiveMultiplier = Settings.bAggressiveMode ? 1.75f : 1.0f;
	const float Volume = FMath::Clamp(Settings.Volume, 0.0f, 4.0f);
	const float Rainfall = FMath::Max(Settings.Rainfall * Volume, 0.0f);
	const float FlowRate = FMath::Clamp(Settings.FlowRate * FMath::Sqrt(FMath::Max(Volume, 0.0f)), 0.0f, 1.0f);
	const float Debris = FMath::Clamp(Settings.Debris, 0.0f, 1.0f);
	const float CapacityFactor = FMath::Max(Settings.SedimentCapacity * FMath::Lerp(0.75f, 1.5f, Debris), 0.0f);
	const float ErosionRate = FMath::Clamp(Settings.ErosionRate * FMath::Max(Settings.Strength, 0.0f) * AggressiveMultiplier, 0.0f, 8.0f);
	const float DepositionRate = FMath::Clamp(Settings.DepositionRate, 0.0f, 1.0f);
	const float Evaporation = FMath::Clamp(Settings.Evaporation, 0.0f, 1.0f);
	const float MinimumSlope = FMath::Max(Settings.MinimumSlope, 0.0f);
	const float BaseRockSoftness = FMath::Clamp(Settings.RockSoftness, 0.0f, 1.0f);
	const float Downcutting = FMath::Clamp(Settings.Downcutting, 0.0f, 2.0f);
	const float Inhibition = FMath::Clamp(Settings.Inhibition, 0.0f, 1.0f);
	const float BaseLevel = FMath::Clamp(Settings.BaseLevel, -1.0f, 1.0f);
	const float SedimentRemoval = FMath::Clamp(Settings.SedimentRemoval, 0.0f, 1.0f);
	const bool bSelectErosionStrength = Settings.SelectiveProcessing == TEXT("ErosionStrength");
	const bool bSelectRockSoftness = Settings.SelectiveProcessing == TEXT("RockSoftness");
	const bool bSelectPrecipitation = Settings.SelectiveProcessing == TEXT("Precipitation");
	const int32 RuntimeSeed = Settings.bDeterministic
		? Settings.Seed
		: Settings.Seed ^ static_cast<int32>(FPlatformTime::Cycles());

	TArray<float> Water, Sediment, NextWater, NextSediment, Flow, Wear, Deposits;
	Water.SetNumZeroed(NumCells);
	Sediment.SetNumZeroed(NumCells);
	if (InitialSediment && InitialSediment->Num() == NumCells)
	{
		for (int32 I = 0; I < NumCells; ++I)
		{
			Sediment[I] = FMath::Max((*InitialSediment)[I], 0.0f);
		}
	}
	NextWater.SetNumZeroed(NumCells);
	NextSediment.SetNumZeroed(NumCells);
	Flow.SetNumZeroed(NumCells);
	Wear.SetNumZeroed(NumCells);
	Deposits.SetNumZeroed(NumCells);

	static const FIntPoint Neighbors[] = {
		FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(0, 1),
		FIntPoint(-1, -1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(1, 1)
	};

	auto Index = [ResolutionX](int32 X, int32 Y) { return Y * ResolutionX + X; };

	for (int32 Iteration = 0; Iteration < Settings.Iterations; ++Iteration)
	{
		for (int32 I = 0; I < NumCells; ++I)
		{
			const float Area = MaskValue(AreaMask, I, NumCells);
			const float SelectivePrecipitation = bSelectPrecipitation ? Area : 1.0f;
			Water[I] += Rainfall * MaskValue(RainfallMask, I, NumCells) * SelectivePrecipitation;
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

		Water = MoveTemp(NextWater);
		Sediment = MoveTemp(NextSediment);
		NextWater.SetNumZeroed(NumCells);
		NextSediment.SetNumZeroed(NumCells);

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
					HeightField.Values[I] += Deposit;
					Sediment[I] -= Deposit;
					Deposits[I] += Deposit;
				}
				else if (Sediment[I] < Capacity && HeightField.Values[I] > BaseLevel)
				{
					const float Area = MaskValue(AreaMask, I, NumCells);
					const float LocalSoftness = bSelectRockSoftness ? BaseRockSoftness * Area : BaseRockSoftness;
					const float Resistance = HardnessResistance(RockHardness, I, NumCells, LocalSoftness);
					const float SelectiveStrength = bSelectErosionStrength ? Area : 1.0f;
					const float DowncuttingFactor = 1.0f + Downcutting * FMath::Clamp(PhysicalSlope, 0.0f, 1.0f);
					const float InhibitionFactor = 1.0f - FMath::Clamp(Wear[I] * Inhibition * 10.0f, 0.0f, 0.95f);
					const float Variation = SeedVariation(RuntimeSeed, Iteration, I);
					const float LocalErosion = ErosionRate
						* MaskValue(ErosionMask, I, NumCells)
						* SelectiveStrength
						* Resistance
						* DowncuttingFactor
						* InhibitionFactor
						* Variation;
					const float MaxToBase = FMath::Max(HeightField.Values[I] - BaseLevel, 0.0f);
					const float Erode = FMath::Min3((Capacity - Sediment[I]) * LocalErosion, 0.02f * AggressiveMultiplier, MaxToBase);
					HeightField.Values[I] -= Erode;
					Sediment[I] += Erode;
					Wear[I] += Erode;
				}
			}
		}

		for (int32 I = 0; I < NumCells; ++I)
		{
			Water[I] *= 1.0f - FMath::Clamp(Evaporation * MaskValue(EvaporationMask, I, NumCells), 0.0f, 1.0f);
			Sediment[I] *= 1.0f - SedimentRemoval;
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
	const TArray<float>* SoilDepth,
	const TArray<float>* AreaMask,
	const TArray<float>* InitialSediment)
{
	OutResult = FGaeaHydraulicErosionResult{};
	if (!InputHeight.IsValid()) return false;
	FGaeaScalarField Height = InputHeight;
	Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
	TArray<float> Flow, Wear, Deposits;
	if (!ApplyInPlace(
		Height,
		HeightScale,
		Settings,
		&Flow,
		RainfallMask,
		ErosionMask,
		DepositionMask,
		EvaporationMask,
		RockHardness,
		SoilDepth,
		&Wear,
		&Deposits,
		AreaMask,
		InitialSediment)) return false;
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
	const FGaeaScalarField* SoilDepth,
	const FGaeaScalarField* AreaMask,
	const FGaeaScalarField* InitialSediment)
{
	return EvaluateWithArrays(
		InputHeight,
		HeightScale,
		Settings,
		OutResult,
		ValuesIfCompatible(RainfallMask, InputHeight.Domain),
		ValuesIfCompatible(ErosionMask, InputHeight.Domain),
		ValuesIfCompatible(DepositionMask, InputHeight.Domain),
		ValuesIfCompatible(EvaporationMask, InputHeight.Domain),
		ValuesIfCompatible(RockHardness, InputHeight.Domain),
		ValuesIfCompatible(SoilDepth, InputHeight.Domain),
		ValuesIfCompatible(AreaMask, InputHeight.Domain),
		ValuesIfCompatible(InitialSediment, InputHeight.Domain));
}
