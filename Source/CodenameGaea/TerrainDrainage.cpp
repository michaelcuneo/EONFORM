#include "TerrainDrainage.h"

namespace
{
	struct FPriorityNode
	{
		float Height = 0.0f;
		int32 Index = INDEX_NONE;
	};

	struct FPriorityNodeGreater
	{
		bool operator()(const FPriorityNode& A, const FPriorityNode& B) const
		{
			return A.Height > B.Height;
		}
	};

	constexpr int32 NeighborCount = 8;
	const FIntPoint NeighborOffsets[NeighborCount] = {
		FIntPoint(-1, 0), FIntPoint(1, 0),
		FIntPoint(0, -1), FIntPoint(0, 1),
		FIntPoint(-1, -1), FIntPoint(1, -1),
		FIntPoint(-1, 1), FIntPoint(1, 1)
	};

	void HeapPush(TArray<FPriorityNode>& Heap, const FPriorityNode& Node)
	{
		Heap.HeapPush(Node, FPriorityNodeGreater());
	}

	bool HeapPop(TArray<FPriorityNode>& Heap, FPriorityNode& OutNode)
	{
		if (Heap.IsEmpty())
		{
			return false;
		}
		Heap.HeapPop(OutNode, FPriorityNodeGreater(), EAllowShrinking::No);
		return true;
	}

	void MarkExteriorOcean(
		const FTerrainHeightField& HeightField,
		TArray<float>& OutExteriorOceanMask)
	{
		const int32 Resolution = HeightField.Resolution;
		const int32 NumCells = HeightField.Data.Num();
		OutExteriorOceanMask.SetNumZeroed(NumCells);

		TArray<int32> Queue;
		Queue.Reserve(NumCells);
		int32 Head = 0;

		auto TrySeed = [&](int32 X, int32 Y)
		{
			const int32 Index = HeightField.Index(X, Y);
			if (HeightField.Data[Index] < 0.0f && OutExteriorOceanMask[Index] == 0.0f)
			{
				OutExteriorOceanMask[Index] = 1.0f;
				Queue.Add(Index);
			}
		};

		for (int32 X = 0; X < Resolution; ++X)
		{
			TrySeed(X, 0);
			TrySeed(X, Resolution - 1);
		}
		for (int32 Y = 1; Y < Resolution - 1; ++Y)
		{
			TrySeed(0, Y);
			TrySeed(Resolution - 1, Y);
		}

		while (Head < Queue.Num())
		{
			const int32 Index = Queue[Head++];
			const int32 X = Index % Resolution;
			const int32 Y = Index / Resolution;

			for (const FIntPoint& Offset : NeighborOffsets)
			{
				const int32 NX = X + Offset.X;
				const int32 NY = Y + Offset.Y;
				if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution)
				{
					continue;
				}

				const int32 Neighbor = HeightField.Index(NX, NY);
				if (HeightField.Data[Neighbor] < 0.0f && OutExteriorOceanMask[Neighbor] == 0.0f)
				{
					OutExteriorOceanMask[Neighbor] = 1.0f;
					Queue.Add(Neighbor);
				}
			}
		}
	}

	void PriorityFlood(
		const FTerrainHeightField& HeightField,
		float HeightScale,
		float FlatEpsilonCm,
		const TArray<float>& ExteriorOceanMask,
		TArray<float>& OutConditioned,
		TArray<float>& OutFillDepthCm,
		TArray<int32>& OutProcessingOrder)
	{
		const int32 Resolution = HeightField.Resolution;
		const int32 NumCells = HeightField.Data.Num();
		const float Epsilon = FMath::Max(FlatEpsilonCm, 0.001f) / FMath::Max(HeightScale, 1.0f);

		OutConditioned = HeightField.Data;
		OutFillDepthCm.SetNumZeroed(NumCells);
		OutProcessingOrder.Reset();
		OutProcessingOrder.Reserve(NumCells);

		TArray<uint8> Visited;
		Visited.SetNumZeroed(NumCells);
		TArray<FPriorityNode> Heap;
		Heap.Reserve(NumCells);

		auto Seed = [&](int32 Index, float Height)
		{
			if (Visited[Index] != 0)
			{
				return;
			}
			Visited[Index] = 1;
			OutConditioned[Index] = Height;
			HeapPush(Heap, { Height, Index });
		};

		bool bSeededOcean = false;
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			if (ExteriorOceanMask[Index] > 0.5f)
			{
				Seed(Index, 0.0f);
				bSeededOcean = true;
			}
		}

		if (!bSeededOcean)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				Seed(HeightField.Index(X, 0), HeightField.At(X, 0));
				Seed(HeightField.Index(X, Resolution - 1), HeightField.At(X, Resolution - 1));
			}
			for (int32 Y = 1; Y < Resolution - 1; ++Y)
			{
				Seed(HeightField.Index(0, Y), HeightField.At(0, Y));
				Seed(HeightField.Index(Resolution - 1, Y), HeightField.At(Resolution - 1, Y));
			}
		}

		FPriorityNode Node;
		while (HeapPop(Heap, Node))
		{
			OutProcessingOrder.Add(Node.Index);
			const int32 X = Node.Index % Resolution;
			const int32 Y = Node.Index / Resolution;

			for (const FIntPoint& Offset : NeighborOffsets)
			{
				const int32 NX = X + Offset.X;
				const int32 NY = Y + Offset.Y;
				if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution)
				{
					continue;
				}

				const int32 Neighbor = HeightField.Index(NX, NY);
				if (Visited[Neighbor] != 0)
				{
					continue;
				}
				Visited[Neighbor] = 1;

				if (ExteriorOceanMask[Neighbor] > 0.5f)
				{
					OutConditioned[Neighbor] = 0.0f;
					HeapPush(Heap, { 0.0f, Neighbor });
					continue;
				}

				const float Original = HeightField.Data[Neighbor];
				const float Filled = FMath::Max(Original, Node.Height + Epsilon);
				OutConditioned[Neighbor] = Filled;
				OutFillDepthCm[Neighbor] = FMath::Max(Filled - Original, 0.0f) * HeightScale;
				HeapPush(Heap, { Filled, Neighbor });
			}
		}
	}

	void BuildReceivers(
		const FTerrainHeightField& HeightField,
		const TArray<float>& Conditioned,
		const TArray<float>& ExteriorOceanMask,
		TArray<int32>& OutReceiver,
		TArray<float>& OutOceanOutletMask)
	{
		const int32 Resolution = HeightField.Resolution;
		const int32 NumCells = HeightField.Data.Num();
		OutReceiver.Init(INDEX_NONE, NumCells);
		OutOceanOutletMask.SetNumZeroed(NumCells);

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = HeightField.Index(X, Y);
				if (ExteriorOceanMask[Index] > 0.5f)
				{
					continue;
				}

				int32 BestNeighbor = INDEX_NONE;
				float BestHeight = Conditioned[Index];

				for (const FIntPoint& Offset : NeighborOffsets)
				{
					const int32 NX = X + Offset.X;
					const int32 NY = Y + Offset.Y;
					if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution)
					{
						continue;
					}

					const int32 Neighbor = HeightField.Index(NX, NY);
					const float NeighborHeight = Conditioned[Neighbor];
					if (NeighborHeight < BestHeight)
					{
						BestHeight = NeighborHeight;
						BestNeighbor = Neighbor;
					}
				}

				OutReceiver[Index] = BestNeighbor;
				if (BestNeighbor != INDEX_NONE && ExteriorOceanMask[BestNeighbor] > 0.5f)
				{
					OutOceanOutletMask[Index] = 1.0f;
				}
			}
		}
	}

	void BuildAccumulation(
		const FTerrainHeightField& HeightField,
		const TArray<float>& Conditioned,
		const TArray<float>& ExteriorOceanMask,
		const TArray<int32>& Receiver,
		TArray<float>& OutFlowAccumulation,
		TArray<float>& OutDrainageAreaCm2)
	{
		const int32 NumCells = HeightField.Data.Num();
		TArray<int32> Order;
		Order.SetNumUninitialized(NumCells);
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			Order[Index] = Index;
		}
		Order.Sort([&Conditioned](int32 A, int32 B)
		{
			return Conditioned[A] > Conditioned[B];
		});

		OutFlowAccumulation.SetNumZeroed(NumCells);
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			if (ExteriorOceanMask[Index] <= 0.5f)
			{
				OutFlowAccumulation[Index] = 1.0f;
			}
		}

		for (const int32 Index : Order)
		{
			const int32 Downstream = Receiver[Index];
			if (Downstream != INDEX_NONE)
			{
				OutFlowAccumulation[Downstream] += OutFlowAccumulation[Index];
			}
		}

		const float CellSize = HeightField.WorldSize / static_cast<float>(HeightField.Resolution - 1);
		const float CellArea = CellSize * CellSize;
		OutDrainageAreaCm2.SetNumUninitialized(NumCells);
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			OutDrainageAreaCm2[Index] = OutFlowAccumulation[Index] * CellArea;
		}
	}

	void BuildWatersheds(
		const TArray<int32>& Receiver,
		const TArray<float>& ExteriorOceanMask,
		TArray<int32>& OutWatershedId)
	{
		const int32 NumCells = Receiver.Num();
		OutWatershedId.Init(INDEX_NONE, NumCells);
		TMap<int32, int32> OutletToWatershed;
		int32 NextWatershed = 0;

		for (int32 Start = 0; Start < NumCells; ++Start)
		{
			if (ExteriorOceanMask[Start] > 0.5f || OutWatershedId[Start] != INDEX_NONE)
			{
				continue;
			}

			TArray<int32> Path;
			int32 Current = Start;
			int32 Watershed = INDEX_NONE;
			for (int32 Safety = 0; Safety <= NumCells; ++Safety)
			{
				if (Current == INDEX_NONE)
				{
					break;
				}
				if (OutWatershedId[Current] != INDEX_NONE)
				{
					Watershed = OutWatershedId[Current];
					break;
				}

				Path.Add(Current);
				const int32 Next = Receiver[Current];
				if (Next == INDEX_NONE || ExteriorOceanMask[Next] > 0.5f)
				{
					const int32 Outlet = Current;
					if (const int32* Existing = OutletToWatershed.Find(Outlet))
					{
						Watershed = *Existing;
					}
					else
					{
						Watershed = NextWatershed++;
						OutletToWatershed.Add(Outlet, Watershed);
					}
					break;
				}
				Current = Next;
			}

			if (Watershed == INDEX_NONE)
			{
				Watershed = NextWatershed++;
			}
			for (const int32 Index : Path)
			{
				OutWatershedId[Index] = Watershed;
			}
		}
	}

	void BuildLakeAndSpillMasks(
		const FTerrainHeightField& HeightField,
		float LakeDepthThresholdCm,
		const TArray<float>& FillDepthCm,
		const TArray<int32>& Receiver,
		TArray<float>& OutLakeMask,
		TArray<float>& OutSpillPointMask)
	{
		const int32 Resolution = HeightField.Resolution;
		const int32 NumCells = HeightField.Data.Num();
		const float Threshold = FMath::Max(LakeDepthThresholdCm, 0.0f);
		OutLakeMask.SetNumZeroed(NumCells);
		OutSpillPointMask.SetNumZeroed(NumCells);

		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			if (FillDepthCm[Index] >= Threshold && HeightField.Data[Index] >= 0.0f)
			{
				OutLakeMask[Index] = 1.0f;
			}
		}

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = HeightField.Index(X, Y);
				if (OutLakeMask[Index] <= 0.5f)
				{
					continue;
				}

				const int32 Downstream = Receiver[Index];
				if (Downstream != INDEX_NONE && OutLakeMask[Downstream] <= 0.5f)
				{
					OutSpillPointMask[Index] = 1.0f;
				}
			}
		}
	}

	void BuildStreamOrder(
		const TArray<float>& Conditioned,
		const TArray<float>& ExteriorOceanMask,
		const TArray<int32>& Receiver,
		TArray<uint8>& OutStreamOrder)
	{
		const int32 NumCells = Receiver.Num();
		TArray<int32> Order;
		Order.SetNumUninitialized(NumCells);
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			Order[Index] = Index;
		}
		Order.Sort([&Conditioned](int32 A, int32 B)
		{
			return Conditioned[A] > Conditioned[B];
		});

		OutStreamOrder.SetNumZeroed(NumCells);
		TArray<uint8> MaxIncoming;
		TArray<uint8> MaxIncomingCount;
		MaxIncoming.SetNumZeroed(NumCells);
		MaxIncomingCount.SetNumZeroed(NumCells);

		for (const int32 Index : Order)
		{
			if (ExteriorOceanMask[Index] > 0.5f)
			{
				continue;
			}

			uint8 OrderValue = 1;
			if (MaxIncoming[Index] > 0)
			{
				OrderValue = MaxIncoming[Index] + (MaxIncomingCount[Index] >= 2 ? 1 : 0);
			}
			OutStreamOrder[Index] = OrderValue;

			const int32 Downstream = Receiver[Index];
			if (Downstream == INDEX_NONE)
			{
				continue;
			}

			if (OrderValue > MaxIncoming[Downstream])
			{
				MaxIncoming[Downstream] = OrderValue;
				MaxIncomingCount[Downstream] = 1;
			}
			else if (OrderValue == MaxIncoming[Downstream])
			{
				MaxIncomingCount[Downstream] = FMath::Min<uint8>(255, MaxIncomingCount[Downstream] + 1);
			}
		}
	}
}

bool FTerrainDrainage::Build(
	const FTerrainHeightField& HeightField,
	float HeightScale,
	const FTerrainDrainageSettings& Settings,
	FTerrainDrainageMaps& OutMaps)
{
	OutMaps = FTerrainDrainageMaps{};
	if (!HeightField.IsValid() || HeightScale <= UE_SMALL_NUMBER)
	{
		return false;
	}

	MarkExteriorOcean(HeightField, OutMaps.ExteriorOceanMask);

	TArray<int32> ProcessingOrder;
	PriorityFlood(
		HeightField,
		HeightScale,
		Settings.FlatEpsilonCm,
		OutMaps.ExteriorOceanMask,
		OutMaps.ConditionedHeight,
		OutMaps.FillDepthCm,
		ProcessingOrder);

	BuildReceivers(
		HeightField,
		OutMaps.ConditionedHeight,
		OutMaps.ExteriorOceanMask,
		OutMaps.Receiver,
		OutMaps.OceanOutletMask);

	BuildAccumulation(
		HeightField,
		OutMaps.ConditionedHeight,
		OutMaps.ExteriorOceanMask,
		OutMaps.Receiver,
		OutMaps.FlowAccumulation,
		OutMaps.DrainageAreaCm2);

	BuildWatersheds(
		OutMaps.Receiver,
		OutMaps.ExteriorOceanMask,
		OutMaps.WatershedId);

	BuildLakeAndSpillMasks(
		HeightField,
		Settings.LakeDepthThresholdCm,
		OutMaps.FillDepthCm,
		OutMaps.Receiver,
		OutMaps.LakeMask,
		OutMaps.SpillPointMask);

	BuildStreamOrder(
		OutMaps.ConditionedHeight,
		OutMaps.ExteriorOceanMask,
		OutMaps.Receiver,
		OutMaps.StreamOrder);

	return OutMaps.IsValidFor(HeightField);
}
