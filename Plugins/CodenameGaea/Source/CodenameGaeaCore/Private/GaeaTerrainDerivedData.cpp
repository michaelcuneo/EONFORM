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
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::FlowAccumulation);
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
			// TArray heaps are max-heaps with TLess semantics; invert the
			// comparison so the lowest filled elevation is popped first.
			return A.Elevation > B.Elevation;
		}
	};

	bool BuildHydrologyFields(
		const FGaeaScalarField& Height,
		FGaeaScalarField& OutDirection,
		FGaeaScalarField& OutAccumulation,
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
		for (const int32 Index : DrainageOrder)
		{
			const int32 To = Receiver[Index];
			if (To != INDEX_NONE)
			{
				Accumulation[To] += Accumulation[Index];
			}
		}

		FGaeaFieldDescriptor DirectionDescriptor;
		DirectionDescriptor.Name = GaeaTerrainFieldNames::FlowDirection;
		DirectionDescriptor.Unit = EGaeaFieldUnit::Unitless;
		DirectionDescriptor.Interpolation = EGaeaInterpolation::Nearest;
		OutDirection.Initialize(Height.Domain, DirectionDescriptor, -1.0f);

		FGaeaFieldDescriptor AccumulationDescriptor;
		AccumulationDescriptor.Name = GaeaTerrainFieldNames::FlowAccumulation;
		AccumulationDescriptor.Unit = EGaeaFieldUnit::Unitless;
		AccumulationDescriptor.Interpolation = EGaeaInterpolation::Bilinear;
		OutAccumulation.Initialize(Height.Domain, AccumulationDescriptor, 1.0f);

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
			}
		}

		if (!OutDirection.IsValid() || !OutAccumulation.IsValid())
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
	if (HasContextFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	const FGaeaScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height)
	{
		return false;
	}

	return FGaeaTerrainContext::Analyze(
		*Height,
		FMath::Max(HeightScale, 1.0f),
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

	if (!EnsureContext(InOutDataset, HeightScale, OutError))
	{
		return false;
	}

	const FGaeaScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height)
	{
		return false;
	}

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

	if (!EnsureContext(InOutDataset, HeightScale, OutError))
	{
		return false;
	}

	const FGaeaScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height)
	{
		return false;
	}

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
	(void)HeightScale;
	if (HasHydrologyFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	const FGaeaScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height || !Height->IsValid())
	{
		return false;
	}

	FGaeaScalarField FlowDirection;
	FGaeaScalarField FlowAccumulation;
	if (!BuildHydrologyFields(*Height, FlowDirection, FlowAccumulation, OutError))
	{
		return false;
	}

	if (!InOutDataset.SetScalarField(MoveTemp(FlowDirection))
		|| !InOutDataset.SetScalarField(MoveTemp(FlowAccumulation)))
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
	if (!EnsureGeology(InOutDataset, HeightScale, Settings, OutError))
	{
		return false;
	}
	if (!EnsureHydrology(InOutDataset, HeightScale, OutError))
	{
		return false;
	}
	return EnsureProcessMasks(InOutDataset, HeightScale, Settings, OutError);
}
