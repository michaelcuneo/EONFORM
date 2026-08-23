#include "GaeaLakeNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaLakeNode
{
	struct FPriorityCell
	{
		float Elevation = 0.0f;
		int32 Index = INDEX_NONE;
	};

	struct FPriorityCellLess
	{
		FORCEINLINE bool operator()(const FPriorityCell& A, const FPriorityCell& B) const
		{
			return A.Elevation > B.Elevation;
		}
	};

	FGaeaTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.DataType = TEXT("Terrain");
		return P;
	}

	FGaeaTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.DataType = TEXT("ScalarField");
		return P;
	}

	FGaeaTerrainParameterDescriptor Num(
		FName Name,
		const TCHAR* Label,
		double Default,
		double Min,
		double Max,
		const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaScalarField MakeScalar(
		const FGaeaGridDomain& Domain,
		FName Name,
		EGaeaFieldUnit Unit = EGaeaFieldUnit::Normalized,
		EGaeaInterpolation Interpolation = EGaeaInterpolation::Bilinear)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = Interpolation;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	const FGaeaTerrainValue* RequireTerrain(const FGaeaTerrainNodeInputs& Inputs, FString& Error)
	{
		const FGaeaTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Lake requires a valid terrain input 'Terrain'.");
			return nullptr;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Lake input has no valid Height field.");
			return nullptr;
		}
		return Input;
	}

	bool BuildDepressionFill(
		const FGaeaScalarField& Height,
		TArray<float>& OutFilled,
		FString& Error)
	{
		const int32 W = Height.Domain.Dimensions.X;
		const int32 H = Height.Domain.Dimensions.Y;
		if (W < 3 || H < 3)
		{
			Error = TEXT("Lake requires at least a 3 x 3 terrain grid.");
			return false;
		}

		const int32 Num = W * H;
		OutFilled.SetNumUninitialized(Num);
		TArray<uint8> Visited;
		Visited.Init(0, Num);
		TArray<FPriorityCell> Heap;
		Heap.Reserve(Num);

		auto IndexOf = [W](int32 X, int32 Y) { return Y * W + X; };
		auto PushBoundary = [&](int32 X, int32 Y)
		{
			const int32 Index = IndexOf(X, Y);
			if (Visited[Index]) return;
			Visited[Index] = 1;
			OutFilled[Index] = Height.AtInterior(X, Y);
			Heap.HeapPush({ OutFilled[Index], Index }, FPriorityCellLess());
		};

		for (int32 X = 0; X < W; ++X)
		{
			PushBoundary(X, 0);
			PushBoundary(X, H - 1);
		}
		for (int32 Y = 1; Y < H - 1; ++Y)
		{
			PushBoundary(0, Y);
			PushBoundary(W - 1, Y);
		}

		static const int32 DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
		static const int32 DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

		while (!Heap.IsEmpty())
		{
			FPriorityCell Cell;
			Heap.HeapPop(Cell, FPriorityCellLess());
			const int32 X = Cell.Index % W;
			const int32 Y = Cell.Index / W;

			for (int32 Direction = 0; Direction < 8; ++Direction)
			{
				const int32 NX = X + DX[Direction];
				const int32 NY = Y + DY[Direction];
				if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
				const int32 NIndex = IndexOf(NX, NY);
				if (Visited[NIndex]) continue;

				Visited[NIndex] = 1;
				OutFilled[NIndex] = FMath::Max(Height.AtInterior(NX, NY), Cell.Elevation);
				Heap.HeapPush({ OutFilled[NIndex], NIndex }, FPriorityCellLess());
			}
		}

		Error.Reset();
		return true;
	}

	bool EvaluateLake(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrain(Inputs, Error);
		if (!Input) return false;

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		const FGaeaScalarField* Source = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Source) return false;

		const int32 W = Source->Domain.Dimensions.X;
		const int32 H = Source->Domain.Dimensions.Y;
		const int32 Num = W * H;
		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		const FVector2d SpacingMeters = Context.PhysicalMetrics.ResolveSampleSpacingMeters(
			Source->Domain.Dimensions,
			Source->Domain.GetCellSize());
		const double CellAreaSquareMeters = Context.PhysicalMetrics.ResolveCellAreaSquareMeters(
			Source->Domain.Dimensions,
			Source->Domain.GetCellSize());

		if (ElevationScaleMeters <= UE_DOUBLE_SMALL_NUMBER
			|| SpacingMeters.X <= UE_DOUBLE_SMALL_NUMBER
			|| SpacingMeters.Y <= UE_DOUBLE_SMALL_NUMBER
			|| CellAreaSquareMeters <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("Lake could not resolve physical terrain metrics.");
			return false;
		}

		const double MinimumAreaKm2 = FMath::Max(Node.GetNumber(TEXT("MinimumAreaKm2"), 0.01), 0.0);
		const double MinimumDepthMeters = FMath::Max(Node.GetNumber(TEXT("MinimumDepthMeters"), 0.5), 0.0);
		const double FillLevel = FMath::Clamp(Node.GetNumber(TEXT("FillLevel"), 1.0), 0.0, 1.0);
		const double ShoreWidthMeters = FMath::Max(Node.GetNumber(TEXT("ShoreWidthMeters"), 30.0), 0.0);
		const double BedCarveMeters = FMath::Max(Node.GetNumber(TEXT("BedCarveMeters"), 0.0), 0.0);
		const double SurfaceOffsetMeters = Node.GetNumber(TEXT("SurfaceOffsetMeters"), 0.0);

		TArray<float> Filled;
		if (!BuildDepressionFill(*Source, Filled, Error)) return false;

		TArray<uint8> Candidate;
		Candidate.Init(0, Num);
		const double CandidateDepthEpsilonMeters = 0.01;
		for (int32 Y = 1; Y < H - 1; ++Y)
		{
			for (int32 X = 1; X < W - 1; ++X)
			{
				const int32 Index = Y * W + X;
				const double DepthMeters = static_cast<double>(Filled[Index] - Source->AtInterior(X, Y)) * ElevationScaleMeters;
				Candidate[Index] = DepthMeters > CandidateDepthEpsilonMeters ? 1 : 0;
			}
		}

		TArray<uint8> Wet;
		Wet.Init(0, Num);
		TArray<float> WaterLevelNormalized;
		WaterLevelNormalized.Init(0.0f, Num);
		TArray<uint8> Visited;
		Visited.Init(0, Num);
		static const int32 DX4[4] = { 1, 0, -1, 0 };
		static const int32 DY4[4] = { 0, 1, 0, -1 };

		for (int32 Start = 0; Start < Num; ++Start)
		{
			if (!Candidate[Start] || Visited[Start]) continue;

			const float BasinSpill = Filled[Start];
			TArray<int32> Component;
			Component.Reserve(64);
			TArray<int32> Queue;
			Queue.Add(Start);
			Visited[Start] = 1;
			int32 Head = 0;
			float BasinMinimum = TNumericLimits<float>::Max();

			while (Head < Queue.Num())
			{
				const int32 Index = Queue[Head++];
				Component.Add(Index);
				const int32 X = Index % W;
				const int32 Y = Index / W;
				BasinMinimum = FMath::Min(BasinMinimum, Source->AtInterior(X, Y));

				for (int32 Direction = 0; Direction < 4; ++Direction)
				{
					const int32 NX = X + DX4[Direction];
					const int32 NY = Y + DY4[Direction];
					if (NX <= 0 || NX >= W - 1 || NY <= 0 || NY >= H - 1) continue;
					const int32 NIndex = NY * W + NX;
					if (!Candidate[NIndex] || Visited[NIndex]) continue;
					if (!FMath::IsNearlyEqual(Filled[NIndex], BasinSpill, 1.e-5f)) continue;
					Visited[NIndex] = 1;
					Queue.Add(NIndex);
				}
			}

			const double BasinAreaKm2 = static_cast<double>(Component.Num()) * CellAreaSquareMeters / 1000000.0;
			const double BasinMaximumDepthMeters = static_cast<double>(BasinSpill - BasinMinimum) * ElevationScaleMeters;
			if (BasinAreaKm2 < MinimumAreaKm2 || BasinMaximumDepthMeters < MinimumDepthMeters) continue;

			const float ActiveWaterLevel = FMath::Lerp(BasinMinimum, BasinSpill, static_cast<float>(FillLevel));
			for (const int32 Index : Component)
			{
				const int32 X = Index % W;
				const int32 Y = Index / W;
				if (Source->AtInterior(X, Y) + static_cast<float>(CandidateDepthEpsilonMeters / ElevationScaleMeters) >= ActiveWaterLevel) continue;
				Wet[Index] = 1;
				WaterLevelNormalized[Index] = ActiveWaterLevel;
			}
		}

		FGaeaScalarField Height = *Source;
		FGaeaScalarField LakeMask = MakeScalar(Source->Domain, TEXT("Lake"));
		FGaeaScalarField Depth = MakeScalar(Source->Domain, TEXT("LakeDepth"), EGaeaFieldUnit::Meters);
		FGaeaScalarField Shore = MakeScalar(Source->Domain, TEXT("LakeShore"));
		FGaeaScalarField Surface = MakeScalar(Source->Domain, TEXT("LakeSurface"), EGaeaFieldUnit::Meters);

		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 Index = Y * W + X;
				if (!Wet[Index]) continue;
				const float WaterLevel = WaterLevelNormalized[Index];
				const double BedMeters = static_cast<double>(Source->AtInterior(X, Y)) * ElevationScaleMeters;
				const double SurfaceMeters = static_cast<double>(WaterLevel) * ElevationScaleMeters + SurfaceOffsetMeters;
				const double DepthMeters = FMath::Max(SurfaceMeters - BedMeters, 0.0);

				LakeMask.AtInterior(X, Y) = 1.0f;
				Depth.AtInterior(X, Y) = static_cast<float>(DepthMeters);
				Surface.AtInterior(X, Y) = static_cast<float>(SurfaceMeters);

				if (BedCarveMeters > 0.0 && DepthMeters > 0.0)
				{
					const double DepthWeight = FMath::Clamp(DepthMeters / FMath::Max(MinimumDepthMeters, 1.0), 0.0, 1.0);
					const double CarveNormalized = BedCarveMeters * DepthWeight / ElevationScaleMeters;
					Height.AtInterior(X, Y) = FMath::Clamp(
						Source->AtInterior(X, Y) - static_cast<float>(CarveNormalized),
						-1.0f,
						1.0f);
				}
			}
		}

		if (ShoreWidthMeters > 0.0)
		{
			TArray<int32> Distance;
			Distance.Init(MAX_int32, Num);
			TArray<int32> Queue;
			Queue.Reserve(Num / 8);

			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					const int32 Index = Y * W + X;
					bool bBoundary = false;
					for (int32 Direction = 0; Direction < 4; ++Direction)
					{
						const int32 NX = X + DX4[Direction];
						const int32 NY = Y + DY4[Direction];
						if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
						if (Wet[NY * W + NX] != Wet[Index])
						{
							bBoundary = true;
							break;
						}
					}
					if (bBoundary)
					{
						Distance[Index] = 0;
						Queue.Add(Index);
					}
				}
			}

			const double StepMeters = FMath::Max(FMath::Min(SpacingMeters.X, SpacingMeters.Y), UE_DOUBLE_SMALL_NUMBER);
			const int32 MaximumSteps = FMath::Clamp(FMath::CeilToInt(ShoreWidthMeters / StepMeters), 1, 512);
			int32 Head = 0;
			while (Head < Queue.Num())
			{
				const int32 Index = Queue[Head++];
				const int32 CurrentDistance = Distance[Index];
				if (CurrentDistance >= MaximumSteps) continue;
				const int32 X = Index % W;
				const int32 Y = Index / W;
				for (int32 Direction = 0; Direction < 4; ++Direction)
				{
					const int32 NX = X + DX4[Direction];
					const int32 NY = Y + DY4[Direction];
					if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
					const int32 NIndex = NY * W + NX;
					if (Distance[NIndex] <= CurrentDistance + 1) continue;
					Distance[NIndex] = CurrentDistance + 1;
					Queue.Add(NIndex);
				}
			}

			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					const int32 Index = Y * W + X;
					if (Distance[Index] == MAX_int32 || Distance[Index] > MaximumSteps) continue;
					const double DistanceMeters = static_cast<double>(Distance[Index]) * StepMeters;
					Shore.AtInterior(X, Y) = static_cast<float>(FMath::Clamp(1.0 - DistanceMeters / ShoreWidthMeters, 0.0, 1.0));
				}
			}
		}

		if (BedCarveMeters > 0.0)
		{
			Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
			if (!Dataset.SetScalarField(MoveTemp(Height)))
			{
				Error = TEXT("Lake could not publish its carved Height field.");
				return false;
			}
		}

		FGaeaScalarField LakeOutput = LakeMask;
		FGaeaScalarField DepthOutput = Depth;
		FGaeaScalarField ShoreOutput = Shore;
		FGaeaScalarField SurfaceOutput = Surface;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(LakeMask))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Depth))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Shore))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Surface)))
		{
			Error = TEXT("Lake could not publish its derived basin fields.");
			return false;
		}

		FGaeaTerrainValue TerrainValue = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!TerrainValue.IsValid())
		{
			Error = TEXT("Lake produced invalid terrain output.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(TerrainValue));
		Out.Outputs.Add(TEXT("Lake"), FGaeaTerrainValue::MakeScalarField(MoveTemp(LakeOutput)));
		Out.Outputs.Add(TEXT("Depth"), FGaeaTerrainValue::MakeScalarField(MoveTemp(DepthOutput)));
		Out.Outputs.Add(TEXT("Shore"), FGaeaTerrainValue::MakeScalarField(MoveTemp(ShoreOutput)));
		Out.Outputs.Add(TEXT("Surface"), FGaeaTerrainValue::MakeScalarField(MoveTemp(SurfaceOutput)));
		Error.Reset();
		return true;
	}
}

void RegisterGaeaLakeNode()
{
	using namespace GaeaLakeNode;

	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Lake;
	Descriptor.DisplayName = TEXT("Lake");
	Descriptor.Category = TEXT("Simulate");
	Descriptor.Description = TEXT("Detects closed drainage basins, resolves their spill elevations in physical world units, and exposes explicit lake, depth, shoreline, and water-surface outputs.");
	Descriptor.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Lake"), TEXT("Lake")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Depth"), TEXT("Depth")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Shore"), TEXT("Shore")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Surface"), TEXT("Surface")));
	Descriptor.Parameters.Add(Num(TEXT("MinimumAreaKm2"), TEXT("Minimum Area (km²)"), 0.01, 0.0, 1000000.0, TEXT("Detection")));
	Descriptor.Parameters.Add(Num(TEXT("MinimumDepthMeters"), TEXT("Minimum Depth (m)"), 0.5, 0.0, 10000.0, TEXT("Detection")));
	Descriptor.Parameters.Add(Num(TEXT("FillLevel"), TEXT("Fill Level"), 1.0, 0.0, 1.0, TEXT("Water")));
	Descriptor.Parameters.Add(Num(TEXT("SurfaceOffsetMeters"), TEXT("Surface Offset (m)"), 0.0, -1000.0, 1000.0, TEXT("Water")));
	Descriptor.Parameters.Add(Num(TEXT("ShoreWidthMeters"), TEXT("Shore Width (m)"), 30.0, 0.0, 100000.0, TEXT("Shore")));
	Descriptor.Parameters.Add(Num(TEXT("BedCarveMeters"), TEXT("Bed Carve (m)"), 0.0, 0.0, 10000.0, TEXT("Bed")));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluateLake);
}
