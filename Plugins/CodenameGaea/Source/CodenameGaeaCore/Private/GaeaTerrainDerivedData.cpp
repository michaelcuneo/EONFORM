#include "GaeaTerrainDerivedData.h"

#include "GaeaTerrainFieldNames.h"

namespace
{
	bool HasContextFields(const FGaeaTerrainDataset& Dataset)
	{
		return Dataset.HasScalarField(GaeaTerrainFieldNames::Elevation)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::SlopeDegrees)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Concavity)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Convexity)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Mountain)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Foothill)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Plains);
	}

	bool HasGeologyFields(const FGaeaTerrainDataset& Dataset)
	{
		return Dataset.HasScalarField(GaeaTerrainFieldNames::RockHardness)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Weathering)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::SoilDepth);
	}

	bool HasProcessMaskFields(const FGaeaTerrainDataset& Dataset)
	{
		return Dataset.HasScalarField(GaeaTerrainFieldNames::Thermal)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Rainfall)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::HydraulicErosion)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Deposition)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Evaporation);
	}

	bool HasHydrologyFields(const FGaeaTerrainDataset& Dataset)
	{
		return Dataset.HasScalarField(GaeaTerrainFieldNames::FlowDirection)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::FlowAccumulation)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::CatchmentAreaKm2)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::DistanceToOutletKm)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::StreamOrder);
	}

	const FGaeaScalarField* RequireHeight(const FGaeaTerrainDataset& Dataset, FString* OutError)
	{
		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
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

	FGaeaScalarField MakeHydrologyField(
		const FGaeaGridDomain& Domain,
		FName Name,
		EGaeaFieldUnit Unit,
		EGaeaInterpolation Interpolation,
		float InitialValue)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = Interpolation;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor, InitialValue);
		return Field;
	}

	bool BuildHydrologyFields(
		const FGaeaScalarField& Height,
		const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
		FGaeaScalarField& OutDirection,
		FGaeaScalarField& OutAccumulation,
		FGaeaScalarField& OutCatchmentArea,
		FGaeaScalarField& OutDistanceToOutlet,
		FGaeaScalarField& OutStreamOrder,
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
		const double CellAreaSquareMeters = PhysicalMetrics.ResolveCellAreaSquareMeters(Dimensions, Height.Domain.GetCellSize());
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
		TArray<int32> FloodRank;
		FloodRank.Init(MAX_int32, Num);
		TArray<FHydrologyCell> Heap;
		Heap.Reserve(Num);

		auto IndexOf = [Width](int32 X, int32 Y) { return Y * Width + X; };
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

		static const int32 DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
		static const int32 DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

		int32 Rank = 0;
		while (!Heap.IsEmpty())
		{
			FHydrologyCell Cell;
			Heap.HeapPop(Cell, FHydrologyCellLess());
			FloodRank[Cell.Index] = Rank++;
			const int32 X = Cell.Index % Width;
			const int32 Y = Cell.Index / Width;

			for (int32 Direction = 0; Direction < 8; ++Direction)
			{
				const int32 NX = X + DX[Direction];
				const int32 NY = Y + DY[Direction];
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
		for (int32 Y = 0; Y < HeightCount; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = IndexOf(X, Y);
				const float CurrentElevation = Filled[Index];
				int32 BestIndex = INDEX_NONE;
				float BestElevation = CurrentElevation;
				int32 BestRank = FloodRank[Index];

				for (int32 Direction = 0; Direction < 8; ++Direction)
				{
					const int32 NX = X + DX[Direction];
					const int32 NY = Y + DY[Direction];
					if (NX < 0 || NX >= Width || NY < 0 || NY >= HeightCount) continue;
					const int32 NIndex = IndexOf(NX, NY);
					const float NElevation = Filled[NIndex];
					const bool bStrictlyLower = NElevation < BestElevation - UE_SMALL_NUMBER;
					const bool bEqualAndCloserToOutlet = FMath::IsNearlyEqual(NElevation, BestElevation)
						&& FloodRank[NIndex] < BestRank;
					if (bStrictlyLower || bEqualAndCloserToOutlet)
					{
						BestIndex = NIndex;
						BestElevation = NElevation;
						BestRank = FloodRank[NIndex];
					}
				}
				Receiver[Index] = BestIndex;
			}
		}

		TArray<int32> DrainageOrder;
		DrainageOrder.Reserve(Num);
		for (int32 Index = 0; Index < Num; ++Index) DrainageOrder.Add(Index);
		DrainageOrder.Sort([&](int32 A, int32 B)
		{
			if (!FMath::IsNearlyEqual(Filled[A], Filled[B])) return Filled[A] > Filled[B];
			return FloodRank[A] > FloodRank[B];
		});

		TArray<float> Accumulation;
		Accumulation.Init(1.0f, Num);
		TArray<int32> HighestUpstreamOrder;
		HighestUpstreamOrder.Init(0, Num);
		TArray<int32> HighestUpstreamCount;
		HighestUpstreamCount.Init(0, Num);
		TArray<int32> StrahlerOrder;
		StrahlerOrder.Init(1, Num);

		for (const int32 Index : DrainageOrder)
		{
			const int32 LocalHighest = HighestUpstreamOrder[Index];
			const int32 LocalOrder = LocalHighest <= 0
				? 1
				: LocalHighest + (HighestUpstreamCount[Index] >= 2 ? 1 : 0);
			StrahlerOrder[Index] = LocalOrder;

			const int32 To = Receiver[Index];
			if (To != INDEX_NONE)
			{
				Accumulation[To] += Accumulation[Index];
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
		DistanceToOutletKm.Init(0.0f, Num);
		for (int32 OrderIndex = DrainageOrder.Num() - 1; OrderIndex >= 0; --OrderIndex)
		{
			const int32 Index = DrainageOrder[OrderIndex];
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

		OutDirection = MakeHydrologyField(Height.Domain, GaeaTerrainFieldNames::FlowDirection, EGaeaFieldUnit::Unitless, EGaeaInterpolation::Nearest, -1.0f);
		OutAccumulation = MakeHydrologyField(Height.Domain, GaeaTerrainFieldNames::FlowAccumulation, EGaeaFieldUnit::Unitless, EGaeaInterpolation::Bilinear, 1.0f);
		OutCatchmentArea = MakeHydrologyField(Height.Domain, GaeaTerrainFieldNames::CatchmentAreaKm2, EGaeaFieldUnit::SquareKilometers, EGaeaInterpolation::Bilinear, 0.0f);
		OutDistanceToOutlet = MakeHydrologyField(Height.Domain, GaeaTerrainFieldNames::DistanceToOutletKm, EGaeaFieldUnit::Kilometers, EGaeaInterpolation::Bilinear, 0.0f);
		OutStreamOrder = MakeHydrologyField(Height.Domain, GaeaTerrainFieldNames::StreamOrder, EGaeaFieldUnit::Unitless, EGaeaInterpolation::Nearest, 1.0f);

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
						if (DX[Direction] == StepX && DY[Direction] == StepY)
						{
							DirectionCode = static_cast<float>(Direction);
							break;
						}
					}
				}

				OutDirection.AtInterior(X, Y) = DirectionCode;
				OutAccumulation.AtInterior(X, Y) = Accumulation[Index];
				OutCatchmentArea.AtInterior(X, Y) = static_cast<float>(static_cast<double>(Accumulation[Index]) * CellAreaSquareMeters / 1000000.0);
				OutDistanceToOutlet.AtInterior(X, Y) = DistanceToOutletKm[Index];
				OutStreamOrder.AtInterior(X, Y) = static_cast<float>(StrahlerOrder[Index]);
			}
		}

		if (!OutDirection.IsValid() || !OutAccumulation.IsValid() || !OutCatchmentArea.IsValid()
			|| !OutDistanceToOutlet.IsValid() || !OutStreamOrder.IsValid())
		{
			if (OutError) *OutError = TEXT("Hydrology produced invalid drainage fields.");
			return false;
		}
		if (OutError) OutError->Reset();
		return true;
	}
}

bool FGaeaTerrainDerivedData::EnsureContext(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	FString* OutError)
{
	return EnsureContext(InOutDataset, HeightScale, FGaeaTerrainPhysicalMetrics(), OutError);
}

bool FGaeaTerrainDerivedData::EnsureContext(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
	FString* OutError)
{
	if (HasContextFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	const FGaeaScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height) return false;

	return FGaeaTerrainContext::Analyze(
		*Height,
		FMath::Max(HeightScale, 1.0f),
		PhysicalMetrics,
		InOutDataset,
		OutError);
}

bool FGaeaTerrainDerivedData::EnsureGeology(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	const FGaeaTerrainDerivedDataSettings& Settings,
	FString* OutError)
{
	if (HasGeologyFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	if (!EnsureContext(InOutDataset, HeightScale, OutError)) return false;
	const FGaeaScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height) return false;

	return FGaeaTerrainGeology::Build(
		*Height,
		Settings.GeologySeed,
		Settings.Geology,
		InOutDataset,
		OutError);
}

bool FGaeaTerrainDerivedData::EnsureProcessMasks(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	const FGaeaTerrainDerivedDataSettings& Settings,
	FString* OutError)
{
	if (HasProcessMaskFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	if (!EnsureContext(InOutDataset, HeightScale, OutError)) return false;
	const FGaeaScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height) return false;

	return FGaeaTerrainContext::BuildProcessMasks(
		*Height,
		Settings.ProcessMasks,
		InOutDataset,
		OutError);
}

bool FGaeaTerrainDerivedData::EnsureHydrology(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	FString* OutError)
{
	return EnsureHydrology(InOutDataset, HeightScale, FGaeaTerrainPhysicalMetrics(), OutError);
}

bool FGaeaTerrainDerivedData::EnsureHydrology(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
	FString* OutError)
{
	(void)HeightScale;
	if (HasHydrologyFields(InOutDataset) && !PhysicalMetrics.IsConfigured())
	{
		if (OutError) OutError->Reset();
		return true;
	}

	const FGaeaScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height || !Height->IsValid()) return false;

	FGaeaScalarField FlowDirection;
	FGaeaScalarField FlowAccumulation;
	FGaeaScalarField CatchmentArea;
	FGaeaScalarField DistanceToOutlet;
	FGaeaScalarField StreamOrder;
	if (!BuildHydrologyFields(
		*Height,
		PhysicalMetrics,
		FlowDirection,
		FlowAccumulation,
		CatchmentArea,
		DistanceToOutlet,
		StreamOrder,
		OutError))
	{
		return false;
	}

	if (!InOutDataset.SetScalarField(MoveTemp(FlowDirection))
		|| !InOutDataset.SetScalarField(MoveTemp(FlowAccumulation))
		|| !InOutDataset.SetScalarField(MoveTemp(CatchmentArea))
		|| !InOutDataset.SetScalarField(MoveTemp(DistanceToOutlet))
		|| !InOutDataset.SetScalarField(MoveTemp(StreamOrder)))
	{
		if (OutError) *OutError = TEXT("Could not publish EONFORM hydrology fields.");
		return false;
	}

	if (OutError) OutError->Reset();
	return true;
}

bool FGaeaTerrainDerivedData::EnsureHydraulicInputs(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	const FGaeaTerrainDerivedDataSettings& Settings,
	FString* OutError)
{
	return EnsureHydraulicInputs(InOutDataset, HeightScale, FGaeaTerrainPhysicalMetrics(), Settings, OutError);
}

bool FGaeaTerrainDerivedData::EnsureHydraulicInputs(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
	const FGaeaTerrainDerivedDataSettings& Settings,
	FString* OutError)
{
	if (!EnsureGeology(InOutDataset, HeightScale, Settings, OutError)) return false;
	if (!EnsureHydrology(InOutDataset, HeightScale, PhysicalMetrics, OutError)) return false;
	return EnsureProcessMasks(InOutDataset, HeightScale, Settings, OutError);
}
