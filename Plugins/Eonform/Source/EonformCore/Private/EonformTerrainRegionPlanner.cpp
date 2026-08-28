#include "EonformTerrainRegionPlanner.h"

bool FEonformTerrainRegionPlanner::BuildRequests(
	const FIntPoint& FullResolution,
	const FVector2d& WorldMinCm,
	const FVector2d& WorldMaxCm,
	const FIntPoint& Sections,
	int32 BorderSamples,
	TArray<FEonformTerrainRegionRequest>& OutRequests,
	FString* OutError)
{
	OutRequests.Reset();

	auto Fail = [&](const TCHAR* Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (FullResolution.X < 2 || FullResolution.Y < 2)
	{
		return Fail(TEXT("Regional terrain planning requires a full resolution of at least 2x2 samples."));
	}
	if (Sections.X < 1 || Sections.Y < 1)
	{
		return Fail(TEXT("Regional terrain planning requires at least one section on each axis."));
	}
	if (Sections.X > FullResolution.X - 1 || Sections.Y > FullResolution.Y - 1)
	{
		return Fail(TEXT("Regional terrain section counts cannot exceed the number of sample intervals."));
	}
	if (BorderSamples < 0)
	{
		return Fail(TEXT("Regional terrain border samples cannot be negative."));
	}
	if (!FMath::IsFinite(WorldMinCm.X) || !FMath::IsFinite(WorldMinCm.Y)
		|| !FMath::IsFinite(WorldMaxCm.X) || !FMath::IsFinite(WorldMaxCm.Y)
		|| WorldMaxCm.X <= WorldMinCm.X || WorldMaxCm.Y <= WorldMinCm.Y)
	{
		return Fail(TEXT("Regional terrain planning requires valid world bounds."));
	}

	const int32 IntervalsX = FullResolution.X - 1;
	const int32 IntervalsY = FullResolution.Y - 1;
	const FVector2d CellSize(
		(WorldMaxCm.X - WorldMinCm.X) / static_cast<double>(IntervalsX),
		(WorldMaxCm.Y - WorldMinCm.Y) / static_cast<double>(IntervalsY));

	OutRequests.Reserve(Sections.X * Sections.Y);
	for (int32 SectionY = 0; SectionY < Sections.Y; ++SectionY)
	{
		const int32 StartY = (SectionY * IntervalsY) / Sections.Y;
		const int32 EndY = ((SectionY + 1) * IntervalsY) / Sections.Y;
		for (int32 SectionX = 0; SectionX < Sections.X; ++SectionX)
		{
			const int32 StartX = (SectionX * IntervalsX) / Sections.X;
			const int32 EndX = ((SectionX + 1) * IntervalsX) / Sections.X;

			FEonformTerrainRegionRequest Request;
			Request.RegionIndex = FIntPoint(SectionX, SectionY);
			Request.StartSample = FIntPoint(StartX, StartY);
			Request.EndSample = FIntPoint(EndX, EndY);
			Request.Resolution = FIntPoint(EndX - StartX + 1, EndY - StartY + 1);
			Request.EvaluationRegion.WorldMinCm = WorldMinCm + FVector2d(
				static_cast<double>(StartX) * CellSize.X,
				static_cast<double>(StartY) * CellSize.Y);
			Request.EvaluationRegion.WorldMaxCm = WorldMinCm + FVector2d(
				static_cast<double>(EndX) * CellSize.X,
				static_cast<double>(EndY) * CellSize.Y);
			Request.EvaluationRegion.BorderSamples = BorderSamples;

			if (!Request.IsValid())
			{
				return Fail(TEXT("Regional terrain planning produced an invalid request."));
			}
			OutRequests.Add(MoveTemp(Request));
		}
	}

	if (OutRequests.Num() != Sections.X * Sections.Y)
	{
		return Fail(TEXT("Regional terrain planning produced the wrong number of requests."));
	}

	if (OutError) OutError->Reset();
	return true;
}
