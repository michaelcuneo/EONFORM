#include "TerrainWater.h"

#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TerrainHeightField.h"
#include "WaterBodyOceanActor.h"
#include "WaterSplineComponent.h"

namespace
{
	struct FContourSegment
	{
		int32 A = INDEX_NONE;
		int32 B = INDEX_NONE;
	};

	int32 HorizontalEdgeKey(int32 X, int32 Y, int32 Resolution)
	{
		return Y * (Resolution - 1) + X;
	}

	int32 VerticalEdgeKey(int32 X, int32 Y, int32 Resolution)
	{
		const int32 HorizontalEdgeCount = Resolution * (Resolution - 1);
		return HorizontalEdgeCount + Y * Resolution + X;
	}

	FVector2D EdgeIntersectionPoint(
		int32 EdgeKey,
		const FTerrainHeightField& HeightField,
		float CellSize,
		float HalfWorldSize)
	{
		const int32 Resolution = HeightField.Resolution;
		const int32 HorizontalEdgeCount = Resolution * (Resolution - 1);

		if (EdgeKey < HorizontalEdgeCount)
		{
			const int32 Y = EdgeKey / (Resolution - 1);
			const int32 X = EdgeKey % (Resolution - 1);
			const float A = HeightField.At(X, Y);
			const float B = HeightField.At(X + 1, Y);
			const float Denominator = A - B;
			const float T = FMath::Clamp(
				FMath::Abs(Denominator) > UE_SMALL_NUMBER ? A / Denominator : 0.5f,
				0.0f,
				1.0f);

			return FVector2D(
				(static_cast<float>(X) + T) * CellSize - HalfWorldSize,
				static_cast<float>(Y) * CellSize - HalfWorldSize);
		}

		const int32 LocalKey = EdgeKey - HorizontalEdgeCount;
		const int32 Y = LocalKey / Resolution;
		const int32 X = LocalKey % Resolution;
		const float A = HeightField.At(X, Y);
		const float B = HeightField.At(X, Y + 1);
		const float Denominator = A - B;
		const float T = FMath::Clamp(
			FMath::Abs(Denominator) > UE_SMALL_NUMBER ? A / Denominator : 0.5f,
			0.0f,
			1.0f);

		return FVector2D(
			static_cast<float>(X) * CellSize - HalfWorldSize,
			(static_cast<float>(Y) + T) * CellSize - HalfWorldSize);
	}

	void AddSegment(TArray<FContourSegment>& Segments, int32 A, int32 B)
	{
		if (A != B)
		{
			Segments.Add({ A, B });
		}
	}

	void AddCellSegments(
		const FTerrainHeightField& HeightField,
		int32 X,
		int32 Y,
		TArray<FContourSegment>& Segments)
	{
		const float BL = HeightField.At(X, Y);
		const float BR = HeightField.At(X + 1, Y);
		const float TR = HeightField.At(X + 1, Y + 1);
		const float TL = HeightField.At(X, Y + 1);

		const int32 CaseIndex =
			(BL >= 0.0f ? 1 : 0)
			| (BR >= 0.0f ? 2 : 0)
			| (TR >= 0.0f ? 4 : 0)
			| (TL >= 0.0f ? 8 : 0);

		if (CaseIndex == 0 || CaseIndex == 15)
		{
			return;
		}

		const int32 Bottom = HorizontalEdgeKey(X, Y, HeightField.Resolution);
		const int32 Right = VerticalEdgeKey(X + 1, Y, HeightField.Resolution);
		const int32 Top = HorizontalEdgeKey(X, Y + 1, HeightField.Resolution);
		const int32 Left = VerticalEdgeKey(X, Y, HeightField.Resolution);

		switch (CaseIndex)
		{
		case 1:  AddSegment(Segments, Bottom, Left); break;
		case 2:  AddSegment(Segments, Bottom, Right); break;
		case 3:  AddSegment(Segments, Left, Right); break;
		case 4:  AddSegment(Segments, Right, Top); break;
		case 5:
		{
			const float Center = (BL + BR + TR + TL) * 0.25f;
			if (Center >= 0.0f)
			{
				AddSegment(Segments, Bottom, Right);
				AddSegment(Segments, Top, Left);
			}
			else
			{
				AddSegment(Segments, Bottom, Left);
				AddSegment(Segments, Right, Top);
			}
			break;
		}
		case 6:  AddSegment(Segments, Bottom, Top); break;
		case 7:  AddSegment(Segments, Left, Top); break;
		case 8:  AddSegment(Segments, Left, Top); break;
		case 9:  AddSegment(Segments, Bottom, Top); break;
		case 10:
		{
			const float Center = (BL + BR + TR + TL) * 0.25f;
			if (Center >= 0.0f)
			{
				AddSegment(Segments, Bottom, Left);
				AddSegment(Segments, Right, Top);
			}
			else
			{
				AddSegment(Segments, Bottom, Right);
				AddSegment(Segments, Top, Left);
			}
			break;
		}
		case 11: AddSegment(Segments, Right, Top); break;
		case 12: AddSegment(Segments, Left, Right); break;
		case 13: AddSegment(Segments, Bottom, Right); break;
		case 14: AddSegment(Segments, Bottom, Left); break;
		default: break;
		}
	}

	float SignedArea(const TArray<FVector2D>& Loop)
	{
		if (Loop.Num() < 3)
		{
			return 0.0f;
		}

		double Area = 0.0;
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const FVector2D& A = Loop[Index];
			const FVector2D& B = Loop[(Index + 1) % Loop.Num()];
			Area += static_cast<double>(A.X) * static_cast<double>(B.Y)
				- static_cast<double>(B.X) * static_cast<double>(A.Y);
		}
		return static_cast<float>(Area * 0.5);
	}

	bool ExtractLargestZeroContour(
		const FTerrainHeightField& HeightField,
		TArray<FVector2D>& OutLoop)
	{
		OutLoop.Reset();
		if (!HeightField.IsValid() || HeightField.Resolution < 2)
		{
			return false;
		}

		const int32 Resolution = HeightField.Resolution;
		const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
		const float HalfWorldSize = HeightField.WorldSize * 0.5f;

		TArray<FContourSegment> Segments;
		Segments.Reserve((Resolution - 1) * (Resolution - 1));
		for (int32 Y = 0; Y < Resolution - 1; ++Y)
		{
			for (int32 X = 0; X < Resolution - 1; ++X)
			{
				AddCellSegments(HeightField, X, Y, Segments);
			}
		}

		if (Segments.Num() == 0)
		{
			return false;
		}

		TMap<int32, TArray<int32>> Adjacency;
		for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
		{
			Adjacency.FindOrAdd(Segments[SegmentIndex].A).Add(SegmentIndex);
			Adjacency.FindOrAdd(Segments[SegmentIndex].B).Add(SegmentIndex);
		}

		TArray<uint8> Visited;
		Visited.SetNumZeroed(Segments.Num());
		float LargestArea = 0.0f;

		for (int32 StartSegmentIndex = 0; StartSegmentIndex < Segments.Num(); ++StartSegmentIndex)
		{
			if (Visited[StartSegmentIndex] != 0)
			{
				continue;
			}

			const FContourSegment& StartSegment = Segments[StartSegmentIndex];
			const int32 StartKey = StartSegment.A;
			int32 CurrentKey = StartSegment.B;
			Visited[StartSegmentIndex] = 1;

			TArray<int32> LoopKeys;
			LoopKeys.Reserve(256);
			LoopKeys.Add(StartKey);
			LoopKeys.Add(CurrentKey);

			bool bClosed = false;
			for (int32 Safety = 0; Safety < Segments.Num() + 1; ++Safety)
			{
				if (CurrentKey == StartKey)
				{
					bClosed = true;
					break;
				}

				const TArray<int32>* Connected = Adjacency.Find(CurrentKey);
				if (!Connected)
				{
					break;
				}

				int32 NextSegmentIndex = INDEX_NONE;
				for (const int32 Candidate : *Connected)
				{
					if (Visited[Candidate] == 0)
					{
						NextSegmentIndex = Candidate;
						break;
					}
				}

				if (NextSegmentIndex == INDEX_NONE)
				{
					break;
				}

				Visited[NextSegmentIndex] = 1;
				const FContourSegment& Next = Segments[NextSegmentIndex];
				CurrentKey = Next.A == CurrentKey ? Next.B : Next.A;
				LoopKeys.Add(CurrentKey);
			}

			if (!bClosed || LoopKeys.Num() < 4)
			{
				continue;
			}

			LoopKeys.Pop(EAllowShrinking::No);
			TArray<FVector2D> Loop;
			Loop.Reserve(LoopKeys.Num());
			for (const int32 Key : LoopKeys)
			{
				Loop.Add(EdgeIntersectionPoint(Key, HeightField, CellSize, HalfWorldSize));
			}

			const float Area = FMath::Abs(SignedArea(Loop));
			if (Area > LargestArea)
			{
				LargestArea = Area;
				OutLoop = MoveTemp(Loop);
			}
		}

		return OutLoop.Num() >= 3;
	}

	void ReduceSplinePointDensity(TArray<FVector2D>& Loop, float MinimumSpacing)
	{
		if (Loop.Num() < 4 || MinimumSpacing <= UE_SMALL_NUMBER)
		{
			return;
		}

		TArray<FVector2D> Reduced;
		Reduced.Reserve(Loop.Num());
		Reduced.Add(Loop[0]);
		const float MinimumSpacingSquared = MinimumSpacing * MinimumSpacing;

		for (int32 Index = 1; Index < Loop.Num(); ++Index)
		{
			if (FVector2D::DistSquared(Reduced.Last(), Loop[Index]) >= MinimumSpacingSquared)
			{
				Reduced.Add(Loop[Index]);
			}
		}

		if (Reduced.Num() >= 3
			&& FVector2D::DistSquared(Reduced[0], Reduced.Last()) < MinimumSpacingSquared)
		{
			Reduced.Pop(EAllowShrinking::No);
		}

		if (Reduced.Num() >= 3)
		{
			Loop = MoveTemp(Reduced);
		}
	}
}

bool FTerrainWater::UpdateOceanFromZeroContour(
	const FTerrainHeightField& HeightField,
	const FTransform& TerrainTransform,
	UWorld* World)
{
	if (!World || !HeightField.IsValid())
	{
		return false;
	}

	AWaterBodyOcean* Ocean = nullptr;
	for (TActorIterator<AWaterBodyOcean> It(World); It; ++It)
	{
		Ocean = *It;
		break;
	}

	if (!Ocean)
	{
		return false;
	}

	TArray<FVector2D> Coastline;
	if (!ExtractLargestZeroContour(HeightField, Coastline))
	{
		return false;
	}

	const float CellSize = HeightField.WorldSize / static_cast<float>(HeightField.Resolution - 1);
	ReduceSplinePointDensity(Coastline, CellSize * 0.65f);
	if (Coastline.Num() < 3)
	{
		return false;
	}

	UWaterSplineComponent* Spline = Ocean->GetWaterSpline();
	if (!Spline)
	{
		return false;
	}

	TArray<FVector> WorldPoints;
	WorldPoints.Reserve(Coastline.Num());
	for (const FVector2D& Point : Coastline)
	{
		WorldPoints.Add(TerrainTransform.TransformPosition(FVector(Point.X, Point.Y, 0.0f)));
	}

	Spline->SetSplinePoints(WorldPoints, ESplineCoordinateSpace::World, false);
	for (int32 PointIndex = 0; PointIndex < WorldPoints.Num(); ++PointIndex)
	{
		Spline->SetSplinePointType(PointIndex, ESplinePointType::Linear, false);
	}
	Spline->SetClosedLoop(true, true);
	Spline->K2_SynchronizeAndBroadcastDataChange();

	return true;
}
