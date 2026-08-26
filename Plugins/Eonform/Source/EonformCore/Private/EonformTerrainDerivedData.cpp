#include "EonformTerrainDerivedData.h"

#include "EonformTerrainFieldNames.h"

namespace
{
	bool HasContextFields(const FEonformTerrainDataset& Dataset)
	{
		return Dataset.HasScalarField(EonformTerrainFieldNames::Elevation)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::SlopeDegrees)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::Concavity)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::Convexity)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::Mountain)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::Foothill)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::Plains);
	}

	bool HasGeologyFields(const FEonformTerrainDataset& Dataset)
	{
		return Dataset.HasScalarField(EonformTerrainFieldNames::RockHardness)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::Weathering)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::SoilDepth);
	}

	bool HasProcessMaskFields(const FEonformTerrainDataset& Dataset)
	{
		return Dataset.HasScalarField(EonformTerrainFieldNames::Thermal)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::Rainfall)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::HydraulicErosion)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::Deposition)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::Evaporation);
	}

	bool HasDrainageFields(const FEonformTerrainDataset& Dataset)
	{
		return Dataset.HasScalarField(EonformTerrainFieldNames::FlowDirection);
	}

	bool HasFlowAnalysisFields(const FEonformTerrainDataset& Dataset)
	{
		return HasDrainageFields(Dataset)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::FlowAccumulation)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::CatchmentAreaKm2);
	}

	bool HasHydrologyNetworkFields(const FEonformTerrainDataset& Dataset)
	{
		return HasFlowAnalysisFields(Dataset)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::DistanceToOutletKm);
	}

	bool HasHydrologyFields(const FEonformTerrainDataset& Dataset)
	{
		return HasHydrologyNetworkFields(Dataset)
			&& Dataset.HasScalarField(EonformTerrainFieldNames::StreamOrder);
	}

	const FEonformScalarField* RequireHeight(const FEonformTerrainDataset& Dataset, FString* OutError)
	{
		const FEonformScalarField* Height = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height && OutError)
		{
			*OutError = TEXT("Derived terrain data requires a Height field.");
		}
		return Height;
	}

	struct FHydrologyCell
	{
		float Elevation = 0.0f;
		int32 Index = INDEX_NONE;
	};

	struct FHydrologyCellLess
	{
		FORCEINLINE bool operator()(const FHydrologyCell& A, const FHydrologyCell& B) const
		{
			return A.Elevation > B.Elevation;
		}
	};

	struct FHydrologyDistanceCell
	{
		double Distance = 0.0;
		int32 Index = INDEX_NONE;
	};

	struct FHydrologyDistanceCellLess
	{
		FORCEINLINE bool operator()(const FHydrologyDistanceCell& A, const FHydrologyDistanceCell& B) const
		{
			return A.Distance > B.Distance;
		}
	};

	FEonformScalarField MakeHydrologyField(
		const FEonformGridDomain& Domain,
		FName Name,
		EEonformFieldUnit Unit,
		EEonformInterpolation Interpolation,
		float InitialValue)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = Interpolation;
		FEonformScalarField Field;
		Field.Initialize(Domain, Descriptor, InitialValue);
		return Field;
	}

	static const int32 HydrologyDX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
	static const int32 HydrologyDY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

	bool BuildDrainageDirection(
		const FEonformScalarField& Height,
		const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
		FEonformScalarField& OutDirection,
		FString* OutError)
	{
		const FIntPoint Dimensions = Height.Domain.Dimensions;
		const int32 Width = Dimensions.X;
		const int32 HeightCount = Dimensions.Y;
		if (Width < 2 || HeightCount < 2)
		{
			if (OutError) *OutError = TEXT("Hydrology requires a terrain grid of at least 2 x 2 samples.");
			return false;
		}

		const FVector2d SampleSpacingMeters = PhysicalMetrics.ResolveSampleSpacingMeters(Dimensions, Height.Domain.GetCellSize());
		if (SampleSpacingMeters.X <= UE_DOUBLE_SMALL_NUMBER || SampleSpacingMeters.Y <= UE_DOUBLE_SMALL_NUMBER)
		{
			if (OutError) *OutError = TEXT("Hydrology could not resolve physical sample spacing.");
			return false;
		}

		const int32 Num = Width * HeightCount;
		TArray<float> Filled;
		Filled.SetNumUninitialized(Num);
		TArray<uint8> Visited;
		Visited.Init(0, Num);
		TArray<FHydrologyCell> Heap;
		Heap.Reserve(Num);

		auto IndexOf = [Width](int32 X, int32 Y) { return Y * Width + X; };
		auto IsBoundary = [Width, HeightCount](int32 X, int32 Y)
		{
			return X == 0 || Y == 0 || X == Width - 1 || Y == HeightCount - 1;
		};
		auto PushBoundary = [&](int32 X, int32 Y)
		{
			const int32 Index = IndexOf(X, Y);
			if (Visited[Index]) return;
			Visited[Index] = 1;
			Filled[Index] = Height.AtInterior(X, Y);
			Heap.HeapPush({ Filled[Index], Index }, FHydrologyCellLess());
		};

		for (int32 X = 0; X < Width; ++X)
		{
			PushBoundary(X, 0);
			PushBoundary(X, HeightCount - 1);
		}
		for (int32 Y = 1; Y < HeightCount - 1; ++Y)
		{
			PushBoundary(0, Y);
			PushBoundary(Width - 1, Y);
		}

		while (!Heap.IsEmpty())
		{
			FHydrologyCell Cell;
			Heap.HeapPop(Cell, FHydrologyCellLess());
			const int32 X = Cell.Index % Width;
			const int32 Y = Cell.Index / Width;

			for (int32 Direction = 0; Direction < 8; ++Direction)
			{
				const int32 NX = X + HydrologyDX[Direction];
				const int32 NY = Y + HydrologyDY[Direction];
				if (NX < 0 || NX >= Width || NY < 0 || NY >= HeightCount) continue;
				const int32 NIndex = IndexOf(NX, NY);
				if (Visited[NIndex]) continue;

				Visited[NIndex] = 1;
				Filled[NIndex] = FMath::Max(Height.AtInterior(NX, NY), Cell.Elevation);
				Heap.HeapPush({ Filled[NIndex], NIndex }, FHydrologyCellLess());
			}
		}

		TArray<int32> Receiver;
		Receiver.Init(INDEX_NONE, Num);
		TArray<uint8> HasStrictReceiver;
		HasStrictReceiver.Init(0, Num);

		for (int32 Y = 0; Y < HeightCount; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = IndexOf(X, Y);
				const float CurrentElevation = Filled[Index];
				double BestSlope = 0.0;
				int32 BestIndex = INDEX_NONE;

				for (int32 Direction = 0; Direction < 8; ++Direction)
				{
					const int32 NX = X + HydrologyDX[Direction];
					const int32 NY = Y + HydrologyDY[Direction];
					if (NX < 0 || NX >= Width || NY < 0 || NY >= HeightCount) continue;
					const int32 NIndex = IndexOf(NX, NY);
					const double Drop = static_cast<double>(CurrentElevation - Filled[NIndex]);
					if (Drop <= static_cast<double>(UE_SMALL_NUMBER)) continue;

					const double StepX = static_cast<double>(HydrologyDX[Direction]) * SampleSpacingMeters.X;
					const double StepY = static_cast<double>(HydrologyDY[Direction]) * SampleSpacingMeters.Y;
					const double Distance = FMath::Sqrt(StepX * StepX + StepY * StepY);
					const double Slope = Drop / FMath::Max(Distance, UE_DOUBLE_SMALL_NUMBER);
					if (Slope > BestSlope)
					{
						BestSlope = Slope;
						BestIndex = NIndex;
					}
				}

				if (BestIndex != INDEX_NONE)
				{
					Receiver[Index] = BestIndex;
					HasStrictReceiver[Index] = 1;
				}
			}
		}

		// Route filled flats by physical distance to a genuine downhill spill or
		// domain-edge outlet. This keeps broad depressions drainage-connected and
		// avoids priority-flood pop-order artifacts.
		const double InfiniteDistance = TNumericLimits<double>::Max();
		TArray<double> FlatDistance;
		FlatDistance.Init(InfiniteDistance, Num);
		TArray<FHydrologyDistanceCell> DistanceHeap;
		DistanceHeap.Reserve(Num);

		for (int32 Y = 0; Y < HeightCount; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = IndexOf(X, Y);
				if (HasStrictReceiver[Index] || IsBoundary(X, Y))
				{
					FlatDistance[Index] = 0.0;
					DistanceHeap.HeapPush({ 0.0, Index }, FHydrologyDistanceCellLess());
				}
			}
		}

		while (!DistanceHeap.IsEmpty())
		{
			FHydrologyDistanceCell Cell;
			DistanceHeap.HeapPop(Cell, FHydrologyDistanceCellLess());
			if (Cell.Distance > FlatDistance[Cell.Index] + UE_DOUBLE_SMALL_NUMBER) continue;

			const int32 X = Cell.Index % Width;
			const int32 Y = Cell.Index / Width;
			for (int32 Direction = 0; Direction < 8; ++Direction)
			{
				const int32 NX = X + HydrologyDX[Direction];
				const int32 NY = Y + HydrologyDY[Direction];
				if (NX < 0 || NX >= Width || NY < 0 || NY >= HeightCount) continue;
				const int32 NIndex = IndexOf(NX, NY);
				if (!FMath::IsNearlyEqual(Filled[NIndex], Filled[Cell.Index])) continue;

				const double StepX = static_cast<double>(HydrologyDX[Direction]) * SampleSpacingMeters.X;
				const double StepY = static_cast<double>(HydrologyDY[Direction]) * SampleSpacingMeters.Y;
				const double StepDistance = FMath::Sqrt(StepX * StepX + StepY * StepY);
				const double CandidateDistance = Cell.Distance + StepDistance;
				if (CandidateDistance + UE_DOUBLE_SMALL_NUMBER < FlatDistance[NIndex])
				{
					FlatDistance[NIndex] = CandidateDistance;
					DistanceHeap.HeapPush({ CandidateDistance, NIndex }, FHydrologyDistanceCellLess());
				}
			}
		}

		for (int32 Y = 0; Y < HeightCount; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = IndexOf(X, Y);
				if (Receiver[Index] != INDEX_NONE || IsBoundary(X, Y)) continue;

				double BestDistance = FlatDistance[Index];
				int32 BestIndex = INDEX_NONE;
				for (int32 Direction = 0; Direction < 8; ++Direction)
				{
					const int32 NX = X + HydrologyDX[Direction];
					const int32 NY = Y + HydrologyDY[Direction];
					if (NX < 0 || NX >= Width || NY < 0 || NY >= HeightCount) continue;
					const int32 NIndex = IndexOf(NX, NY);
					if (!FMath::IsNearlyEqual(Filled[NIndex], Filled[Index])) continue;
					if (FlatDistance[NIndex] + UE_DOUBLE_SMALL_NUMBER < BestDistance)
					{
						BestDistance = FlatDistance[NIndex];
						BestIndex = NIndex;
					}
				}
				Receiver[Index] = BestIndex;
			}
		}

		OutDirection = MakeHydrologyField(
			Height.Domain,
			EonformTerrainFieldNames::FlowDirection,
			EEonformFieldUnit::Unitless,
			EEonformInterpolation::Nearest,
			-1.0f);

		for (int32 Y = 0; Y < HeightCount; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = IndexOf(X, Y);
				const int32 To = Receiver[Index];
				float DirectionCode = -1.0f;
				if (To != INDEX_NONE)
				{
					const int32 TX = To % Width;
					const int32 TY = To / Width;
					const int32 StepX = FMath::Clamp(TX - X, -1, 1);
					const int32 StepY = FMath::Clamp(TY - Y, -1, 1);
					for (int32 Direction = 0; Direction < 8; ++Direction)
					{
						if (HydrologyDX[Direction] == StepX && HydrologyDY[Direction] == StepY)
						{
							DirectionCode = static_cast<float>(Direction);
							break;
						}
					}
				}
				OutDirection.AtInterior(X, Y) = DirectionCode;
			}
		}

		if (!OutDirection.IsValid())
		{
			if (OutError) *OutError = TEXT("Hydrology produced an invalid drainage direction field.");
			return false;
		}
		if (OutError) OutError->Reset();
		return true;
	}

	bool BuildReceiverGraph(
		const FEonformScalarField& Direction,
		TArray<int32>& OutReceiver,
		TArray<int32>& OutUpstreamCount,
		TArray<int32>& OutTopologicalOrder,
		FString* OutError)
	{
		const int32 Width = Direction.Domain.Dimensions.X;
		const int32 HeightCount = Direction.Domain.Dimensions.Y;
		const int32 Num = Width * HeightCount;
		OutReceiver.Init(INDEX_NONE, Num);
		OutUpstreamCount.Init(0, Num);

		for (int32 Y = 0; Y < HeightCount; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = Y * Width + X;
				const int32 Code = FMath::RoundToInt(Direction.AtInterior(X, Y));
				if (Code < 0 || Code > 7) continue;
				const int32 NX = X + HydrologyDX[Code];
				const int32 NY = Y + HydrologyDY[Code];
				if (NX < 0 || NX >= Width || NY < 0 || NY >= HeightCount) continue;
				const int32 To = NY * Width + NX;
				if (To == Index) continue;
				OutReceiver[Index] = To;
				++OutUpstreamCount[To];
			}
		}

		TArray<int32> RemainingUpstream = OutUpstreamCount;
		TArray<int32> Queue;
		Queue.Reserve(Num);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			if (RemainingUpstream[Index] == 0) Queue.Add(Index);
		}

		OutTopologicalOrder.Reset(Num);
		int32 Head = 0;
		while (Head < Queue.Num())
		{
			const int32 Index = Queue[Head++];
			OutTopologicalOrder.Add(Index);
			const int32 To = OutReceiver[Index];
			if (To != INDEX_NONE && --RemainingUpstream[To] == 0)
			{
				Queue.Add(To);
			}
		}

		if (OutTopologicalOrder.Num() != Num)
		{
			if (OutError) *OutError = TEXT("Hydrology drainage graph contains a cycle.");
			return false;
		}
		return true;
	}

	bool BuildFlowProducts(
		const FEonformScalarField& Direction,
		const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
		bool bNeedDistance,
		bool bNeedStreamOrder,
		FEonformScalarField& OutAccumulation,
		FEonformScalarField& OutCatchmentArea,
		FEonformScalarField* OutDistanceToOutlet,
		FEonformScalarField* OutStreamOrder,
		FString* OutError)
	{
		const int32 Width = Direction.Domain.Dimensions.X;
		const int32 HeightCount = Direction.Domain.Dimensions.Y;
		const int32 Num = Width * HeightCount;
		const FVector2d SampleSpacingMeters = PhysicalMetrics.ResolveSampleSpacingMeters(Direction.Domain.Dimensions, Direction.Domain.GetCellSize());
		const double CellAreaSquareMeters = PhysicalMetrics.ResolveCellAreaSquareMeters(Direction.Domain.Dimensions, Direction.Domain.GetCellSize());

		TArray<int32> Receiver;
		TArray<int32> UpstreamCount;
		TArray<int32> TopologicalOrder;
		if (!BuildReceiverGraph(Direction, Receiver, UpstreamCount, TopologicalOrder, OutError)) return false;

		TArray<float> Accumulation;
		Accumulation.Init(1.0f, Num);
		TArray<int32> HighestUpstreamOrder;
		TArray<int32> HighestUpstreamCount;
		TArray<int32> StrahlerOrder;
		if (bNeedStreamOrder)
		{
			HighestUpstreamOrder.Init(0, Num);
			HighestUpstreamCount.Init(0, Num);
			StrahlerOrder.Init(1, Num);
		}

		for (const int32 Index : TopologicalOrder)
		{
			int32 LocalOrder = 1;
			if (bNeedStreamOrder)
			{
				const int32 LocalHighest = HighestUpstreamOrder[Index];
				LocalOrder = LocalHighest <= 0 ? 1 : LocalHighest + (HighestUpstreamCount[Index] >= 2 ? 1 : 0);
				StrahlerOrder[Index] = LocalOrder;
			}

			const int32 To = Receiver[Index];
			if (To == INDEX_NONE) continue;
			Accumulation[To] += Accumulation[Index];

			if (bNeedStreamOrder)
			{
				if (LocalOrder > HighestUpstreamOrder[To])
				{
					HighestUpstreamOrder[To] = LocalOrder;
					HighestUpstreamCount[To] = 1;
				}
				else if (LocalOrder == HighestUpstreamOrder[To])
				{
					++HighestUpstreamCount[To];
				}
			}
		}

		TArray<float> DistanceToOutletKm;
		if (bNeedDistance)
		{
			DistanceToOutletKm.Init(0.0f, Num);
			for (int32 OrderIndex = TopologicalOrder.Num() - 1; OrderIndex >= 0; --OrderIndex)
			{
				const int32 Index = TopologicalOrder[OrderIndex];
				const int32 To = Receiver[Index];
				if (To == INDEX_NONE) continue;
				const int32 X = Index % Width;
				const int32 Y = Index / Width;
				const int32 TX = To % Width;
				const int32 TY = To / Width;
				const double StepX = static_cast<double>(TX - X) * SampleSpacingMeters.X;
				const double StepY = static_cast<double>(TY - Y) * SampleSpacingMeters.Y;
				const double StepKm = FMath::Sqrt(StepX * StepX + StepY * StepY) / 1000.0;
				DistanceToOutletKm[Index] = static_cast<float>(StepKm + static_cast<double>(DistanceToOutletKm[To]));
			}
		}

		OutAccumulation = MakeHydrologyField(Direction.Domain, EonformTerrainFieldNames::FlowAccumulation, EEonformFieldUnit::Unitless, EEonformInterpolation::Bilinear, 1.0f);
		OutCatchmentArea = MakeHydrologyField(Direction.Domain, EonformTerrainFieldNames::CatchmentAreaKm2, EEonformFieldUnit::SquareKilometers, EEonformInterpolation::Bilinear, 0.0f);
		if (bNeedDistance && OutDistanceToOutlet)
		{
			*OutDistanceToOutlet = MakeHydrologyField(Direction.Domain, EonformTerrainFieldNames::DistanceToOutletKm, EEonformFieldUnit::Kilometers, EEonformInterpolation::Bilinear, 0.0f);
		}
		if (bNeedStreamOrder && OutStreamOrder)
		{
			*OutStreamOrder = MakeHydrologyField(Direction.Domain, EonformTerrainFieldNames::StreamOrder, EEonformFieldUnit::Unitless, EEonformInterpolation::Nearest, 1.0f);
		}

		for (int32 Y = 0; Y < HeightCount; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = Y * Width + X;
				OutAccumulation.AtInterior(X, Y) = Accumulation[Index];
				OutCatchmentArea.AtInterior(X, Y) = static_cast<float>(static_cast<double>(Accumulation[Index]) * CellAreaSquareMeters / 1000000.0);
				if (bNeedDistance && OutDistanceToOutlet) OutDistanceToOutlet->AtInterior(X, Y) = DistanceToOutletKm[Index];
				if (bNeedStreamOrder && OutStreamOrder) OutStreamOrder->AtInterior(X, Y) = static_cast<float>(StrahlerOrder[Index]);
			}
		}

		if (!OutAccumulation.IsValid() || !OutCatchmentArea.IsValid()
			|| (bNeedDistance && (!OutDistanceToOutlet || !OutDistanceToOutlet->IsValid()))
			|| (bNeedStreamOrder && (!OutStreamOrder || !OutStreamOrder->IsValid())))
		{
			if (OutError) *OutError = TEXT("Hydrology produced invalid flow-analysis fields.");
			return false;
		}
		if (OutError) OutError->Reset();
		return true;
	}
}

bool FEonformTerrainDerivedData::EnsureContext(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	FString* OutError)
{
	return EnsureContext(InOutDataset, HeightScale, FEonformTerrainPhysicalMetrics(), OutError);
}

bool FEonformTerrainDerivedData::EnsureContext(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
	FString* OutError)
{
	if (HasContextFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	const FEonformScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height) return false;

	return FEonformTerrainContext::Analyze(
		*Height,
		FMath::Max(HeightScale, 1.0f),
		PhysicalMetrics,
		InOutDataset,
		OutError);
}

bool FEonformTerrainDerivedData::EnsureGeology(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	const FEonformTerrainDerivedDataSettings& Settings,
	FString* OutError)
{
	if (HasGeologyFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	if (!EnsureContext(InOutDataset, HeightScale, OutError)) return false;
	const FEonformScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height) return false;

	return FEonformTerrainGeology::Build(
		*Height,
		Settings.GeologySeed,
		Settings.Geology,
		InOutDataset,
		OutError);
}

bool FEonformTerrainDerivedData::EnsureProcessMasks(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	const FEonformTerrainDerivedDataSettings& Settings,
	FString* OutError)
{
	if (HasProcessMaskFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	if (!EnsureContext(InOutDataset, HeightScale, OutError)) return false;
	const FEonformScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height) return false;

	return FEonformTerrainContext::BuildProcessMasks(
		*Height,
		Settings.ProcessMasks,
		InOutDataset,
		OutError);
}

bool FEonformTerrainDerivedData::EnsureDrainage(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	FString* OutError)
{
	return EnsureDrainage(InOutDataset, HeightScale, FEonformTerrainPhysicalMetrics(), OutError);
}

bool FEonformTerrainDerivedData::EnsureDrainage(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
	FString* OutError)
{
	(void)HeightScale;
	if (HasDrainageFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}
	const FEonformScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height || !Height->IsValid()) return false;

	FEonformScalarField Direction;
	if (!BuildDrainageDirection(*Height, PhysicalMetrics, Direction, OutError)) return false;
	if (!InOutDataset.SetHeightDerivedScalarField(MoveTemp(Direction)))
	{
		if (OutError) *OutError = TEXT("Could not publish EONFORM drainage direction.");
		return false;
	}
	if (OutError) OutError->Reset();
	return true;
}

bool FEonformTerrainDerivedData::EnsureFlowAnalysis(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	FString* OutError)
{
	return EnsureFlowAnalysis(InOutDataset, HeightScale, FEonformTerrainPhysicalMetrics(), OutError);
}

bool FEonformTerrainDerivedData::EnsureFlowAnalysis(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
	FString* OutError)
{
	if (HasFlowAnalysisFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}
	if (!EnsureDrainage(InOutDataset, HeightScale, PhysicalMetrics, OutError)) return false;
	const FEonformScalarField* Direction = InOutDataset.FindScalarField(EonformTerrainFieldNames::FlowDirection);
	if (!Direction) return false;

	FEonformScalarField Accumulation;
	FEonformScalarField Catchment;
	if (!BuildFlowProducts(*Direction, PhysicalMetrics, false, false, Accumulation, Catchment, nullptr, nullptr, OutError)) return false;
	if (!InOutDataset.SetHeightDerivedScalarField(MoveTemp(Accumulation))
		|| !InOutDataset.SetHeightDerivedScalarField(MoveTemp(Catchment)))
	{
		if (OutError) *OutError = TEXT("Could not publish EONFORM flow-analysis fields.");
		return false;
	}
	if (OutError) OutError->Reset();
	return true;
}

bool FEonformTerrainDerivedData::EnsureHydrologyNetwork(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	FString* OutError)
{
	return EnsureHydrologyNetwork(InOutDataset, HeightScale, FEonformTerrainPhysicalMetrics(), OutError);
}

bool FEonformTerrainDerivedData::EnsureHydrologyNetwork(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
	FString* OutError)
{
	if (HasHydrologyNetworkFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}
	if (!EnsureFlowAnalysis(InOutDataset, HeightScale, PhysicalMetrics, OutError)) return false;
	const FEonformScalarField* Direction = InOutDataset.FindScalarField(EonformTerrainFieldNames::FlowDirection);
	if (!Direction) return false;

	FEonformScalarField Accumulation;
	FEonformScalarField Catchment;
	FEonformScalarField Distance;
	if (!BuildFlowProducts(*Direction, PhysicalMetrics, true, false, Accumulation, Catchment, &Distance, nullptr, OutError)) return false;
	if (!InOutDataset.SetHeightDerivedScalarField(MoveTemp(Distance)))
	{
		if (OutError) *OutError = TEXT("Could not publish EONFORM outlet-distance field.");
		return false;
	}
	if (OutError) OutError->Reset();
	return true;
}

bool FEonformTerrainDerivedData::EnsureHydrology(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	FString* OutError)
{
	return EnsureHydrology(InOutDataset, HeightScale, FEonformTerrainPhysicalMetrics(), OutError);
}

bool FEonformTerrainDerivedData::EnsureHydrology(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
	FString* OutError)
{
	if (HasHydrologyFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}
	if (!EnsureFlowAnalysis(InOutDataset, HeightScale, PhysicalMetrics, OutError)) return false;
	const FEonformScalarField* Direction = InOutDataset.FindScalarField(EonformTerrainFieldNames::FlowDirection);
	if (!Direction) return false;

	const bool bNeedDistance = !InOutDataset.HasScalarField(EonformTerrainFieldNames::DistanceToOutletKm);
	const bool bNeedOrder = !InOutDataset.HasScalarField(EonformTerrainFieldNames::StreamOrder);
	FEonformScalarField Accumulation;
	FEonformScalarField Catchment;
	FEonformScalarField Distance;
	FEonformScalarField StreamOrder;
	if (!BuildFlowProducts(
		*Direction,
		PhysicalMetrics,
		bNeedDistance,
		bNeedOrder,
		Accumulation,
		Catchment,
		bNeedDistance ? &Distance : nullptr,
		bNeedOrder ? &StreamOrder : nullptr,
		OutError)) return false;

	if ((bNeedDistance && !InOutDataset.SetHeightDerivedScalarField(MoveTemp(Distance)))
		|| (bNeedOrder && !InOutDataset.SetHeightDerivedScalarField(MoveTemp(StreamOrder))))
	{
		if (OutError) *OutError = TEXT("Could not publish EONFORM hydrology network fields.");
		return false;
	}
	if (OutError) OutError->Reset();
	return true;
}

bool FEonformTerrainDerivedData::EnsureHydraulicInputs(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	const FEonformTerrainDerivedDataSettings& Settings,
	FString* OutError)
{
	return EnsureHydraulicInputs(InOutDataset, HeightScale, FEonformTerrainPhysicalMetrics(), Settings, OutError);
}

bool FEonformTerrainDerivedData::EnsureHydraulicInputs(
	FEonformTerrainDataset& InOutDataset,
	float HeightScale,
	const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
	const FEonformTerrainDerivedDataSettings& Settings,
	FString* OutError)
{
	if (!EnsureGeology(InOutDataset, HeightScale, Settings, OutError)) return false;
	return EnsureProcessMasks(InOutDataset, HeightScale, Settings, OutError);
}
