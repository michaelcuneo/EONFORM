#include "GaeaSeaNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaSeaNode
{
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
		EGaeaFieldUnit Unit = EGaeaFieldUnit::Normalized)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
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
			Error = TEXT("Sea requires a valid terrain input 'Terrain'.");
			return nullptr;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Sea input has no valid Height field.");
			return nullptr;
		}
		return Input;
	}

	bool EvaluateSea(
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
		if (W < 2 || H < 2)
		{
			Error = TEXT("Sea requires at least a 2 x 2 terrain grid.");
			return false;
		}

		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		const FVector2d SpacingMeters = Context.PhysicalMetrics.ResolveSampleSpacingMeters(
			Source->Domain.Dimensions,
			Source->Domain.GetCellSize());
		if (ElevationScaleMeters <= UE_DOUBLE_SMALL_NUMBER
			|| SpacingMeters.X <= UE_DOUBLE_SMALL_NUMBER
			|| SpacingMeters.Y <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("Sea could not resolve physical terrain metrics.");
			return false;
		}

		const double ShoreWidthMeters = FMath::Max(Node.GetNumber(TEXT("ShoreWidthMeters"), 120.0), 0.0);
		const double ShelfWidthMeters = FMath::Max(Node.GetNumber(TEXT("ShelfWidthMeters"), 1500.0), 0.0);
		const double ShelfDepthMeters = FMath::Max(Node.GetNumber(TEXT("ShelfDepthMeters"), 120.0), 0.0);
		const double CoastalErosionMeters = FMath::Max(Node.GetNumber(TEXT("CoastalErosionMeters"), 0.0), 0.0);
		const double BeachDepositionMeters = FMath::Max(Node.GetNumber(TEXT("BeachDepositionMeters"), 0.0), 0.0);

		const int32 NumCells = W * H;
		TArray<uint8> SeaConnected;
		SeaConnected.Init(0, NumCells);
		TArray<int32> Queue;
		Queue.Reserve(NumCells / 4);

		auto IndexOf = [W](int32 X, int32 Y) { return Y * W + X; };
		auto IsBelowSeaLevel = [&](int32 X, int32 Y)
		{
			// EONFORM's physical datum is fixed: sea level is always exactly 0 m.
			return static_cast<double>(Source->AtInterior(X, Y)) * ElevationScaleMeters < 0.0;
		};
		auto AddBoundary = [&](int32 X, int32 Y)
		{
			if (!IsBelowSeaLevel(X, Y)) return;
			const int32 Index = IndexOf(X, Y);
			if (SeaConnected[Index]) return;
			SeaConnected[Index] = 1;
			Queue.Add(Index);
		};

		for (int32 X = 0; X < W; ++X)
		{
			AddBoundary(X, 0);
			AddBoundary(X, H - 1);
		}
		for (int32 Y = 1; Y < H - 1; ++Y)
		{
			AddBoundary(0, Y);
			AddBoundary(W - 1, Y);
		}

		static const int32 DX4[4] = { 1, 0, -1, 0 };
		static const int32 DY4[4] = { 0, 1, 0, -1 };
		int32 QueueHead = 0;
		while (QueueHead < Queue.Num())
		{
			const int32 Index = Queue[QueueHead++];
			const int32 X = Index % W;
			const int32 Y = Index / W;
			for (int32 Direction = 0; Direction < 4; ++Direction)
			{
				const int32 NX = X + DX4[Direction];
				const int32 NY = Y + DY4[Direction];
				if (NX < 0 || NX >= W || NY < 0 || NY >= H || !IsBelowSeaLevel(NX, NY)) continue;
				const int32 NIndex = IndexOf(NX, NY);
				if (SeaConnected[NIndex]) continue;
				SeaConnected[NIndex] = 1;
				Queue.Add(NIndex);
			}
		}

		FGaeaScalarField SeaMask = MakeScalar(Source->Domain, TEXT("Sea"));
		FGaeaScalarField Depth = MakeScalar(Source->Domain, TEXT("SeaDepth"), EGaeaFieldUnit::Meters);
		FGaeaScalarField Shore = MakeScalar(Source->Domain, TEXT("SeaShore"));
		FGaeaScalarField Surface = MakeScalar(Source->Domain, TEXT("SeaSurface"), EGaeaFieldUnit::Meters);
		FGaeaScalarField Height = *Source;

		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 Index = IndexOf(X, Y);
				if (!SeaConnected[Index]) continue;
				const double BedMeters = static_cast<double>(Source->AtInterior(X, Y)) * ElevationScaleMeters;
				SeaMask.AtInterior(X, Y) = 1.0f;
				Depth.AtInterior(X, Y) = static_cast<float>(FMath::Max(-BedMeters, 0.0));
				Surface.AtInterior(X, Y) = 0.0f;
			}
		}

		// Distance from the actual land/sea boundary. This is intentionally derived
		// from the connected ocean mask rather than raw elevation so inland basins do
		// not acquire marine shoreline behaviour.
		TArray<int32> CoastDistance;
		CoastDistance.Init(MAX_int32, NumCells);
		Queue.Reset();
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 Index = IndexOf(X, Y);
				bool bCoast = false;
				for (int32 Direction = 0; Direction < 4; ++Direction)
				{
					const int32 NX = X + DX4[Direction];
					const int32 NY = Y + DY4[Direction];
					if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
					if (SeaConnected[Index] != SeaConnected[IndexOf(NX, NY)])
					{
						bCoast = true;
						break;
					}
				}
				if (bCoast)
				{
					CoastDistance[Index] = 0;
					Queue.Add(Index);
				}
			}
		}

		const double StepMeters = FMath::Max(FMath::Min(SpacingMeters.X, SpacingMeters.Y), UE_DOUBLE_SMALL_NUMBER);
		const double MaximumInfluenceMeters = FMath::Max(ShoreWidthMeters, ShelfWidthMeters);
		const int32 MaximumSteps = MaximumInfluenceMeters > 0.0
			? FMath::Clamp(FMath::CeilToInt(MaximumInfluenceMeters / StepMeters), 1, 4096)
			: 0;

		QueueHead = 0;
		while (QueueHead < Queue.Num())
		{
			const int32 Index = Queue[QueueHead++];
			const int32 CurrentDistance = CoastDistance[Index];
			if (CurrentDistance >= MaximumSteps) continue;
			const int32 X = Index % W;
			const int32 Y = Index / W;
			for (int32 Direction = 0; Direction < 4; ++Direction)
			{
				const int32 NX = X + DX4[Direction];
				const int32 NY = Y + DY4[Direction];
				if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
				const int32 NIndex = IndexOf(NX, NY);
				if (CoastDistance[NIndex] <= CurrentDistance + 1) continue;
				CoastDistance[NIndex] = CurrentDistance + 1;
				Queue.Add(NIndex);
			}
		}

		bool bHeightChanged = false;
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 Index = IndexOf(X, Y);
				if (CoastDistance[Index] == MAX_int32) continue;
				const double DistanceMeters = static_cast<double>(CoastDistance[Index]) * StepMeters;

				if (ShoreWidthMeters > 0.0 && DistanceMeters <= ShoreWidthMeters)
				{
					const double ShoreWeight = FMath::Clamp(1.0 - DistanceMeters / ShoreWidthMeters, 0.0, 1.0);
					Shore.AtInterior(X, Y) = static_cast<float>(ShoreWeight);

					if (!SeaConnected[Index] && (CoastalErosionMeters > 0.0 || BeachDepositionMeters > 0.0))
					{
						const double SourceMeters = static_cast<double>(Source->AtInterior(X, Y)) * ElevationScaleMeters;
						const double NearSeaWeight = ShoreWeight * FMath::Clamp(1.0 - FMath::Abs(SourceMeters) / FMath::Max(ShoreWidthMeters, 1.0), 0.0, 1.0);
						const double DeltaMeters = (BeachDepositionMeters - CoastalErosionMeters) * NearSeaWeight;
						if (!FMath::IsNearlyZero(DeltaMeters))
						{
							Height.AtInterior(X, Y) = FMath::Clamp(
								Source->AtInterior(X, Y) + static_cast<float>(DeltaMeters / ElevationScaleMeters),
								-1.0f,
								1.0f);
							bHeightChanged = true;
						}
					}
				}

				if (SeaConnected[Index] && ShelfWidthMeters > 0.0 && ShelfDepthMeters > 0.0 && DistanceMeters <= ShelfWidthMeters)
				{
					const double ShelfT = FMath::Clamp(DistanceMeters / ShelfWidthMeters, 0.0, 1.0);
					const double TargetBedMeters = -FMath::Lerp(0.5, ShelfDepthMeters, ShelfT * ShelfT);
					const double SourceBedMeters = static_cast<double>(Source->AtInterior(X, Y)) * ElevationScaleMeters;
					if (SourceBedMeters < TargetBedMeters)
					{
						const double Blend = 1.0 - ShelfT;
						const double NewBedMeters = FMath::Lerp(SourceBedMeters, TargetBedMeters, Blend * 0.35);
						Height.AtInterior(X, Y) = FMath::Clamp(static_cast<float>(NewBedMeters / ElevationScaleMeters), -1.0f, 1.0f);
						bHeightChanged = true;
					}
				}
			}
		}

		if (bHeightChanged)
		{
			Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
			if (!Dataset.SetScalarField(MoveTemp(Height)))
			{
				Error = TEXT("Sea could not publish its coastal Height field.");
				return false;
			}
		}

		FGaeaScalarField SeaOutput = SeaMask;
		FGaeaScalarField DepthOutput = Depth;
		FGaeaScalarField ShoreOutput = Shore;
		FGaeaScalarField SurfaceOutput = Surface;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(SeaMask))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Depth))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Shore))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Surface)))
		{
			Error = TEXT("Sea could not publish its derived marine fields.");
			return false;
		}

		FGaeaTerrainValue TerrainValue = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!TerrainValue.IsValid())
		{
			Error = TEXT("Sea produced invalid terrain output.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(TerrainValue));
		Out.Outputs.Add(TEXT("Sea"), FGaeaTerrainValue::MakeScalarField(MoveTemp(SeaOutput)));
		Out.Outputs.Add(TEXT("Depth"), FGaeaTerrainValue::MakeScalarField(MoveTemp(DepthOutput)));
		Out.Outputs.Add(TEXT("Shore"), FGaeaTerrainValue::MakeScalarField(MoveTemp(ShoreOutput)));
		Out.Outputs.Add(TEXT("Surface"), FGaeaTerrainValue::MakeScalarField(MoveTemp(SurfaceOutput)));
		Error.Reset();
		return true;
	}
}

void RegisterGaeaSeaNode()
{
	using namespace GaeaSeaNode;

	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Sea;
	Descriptor.DisplayName = TEXT("Sea");
	Descriptor.Category = TEXT("Simulate");
	Descriptor.Description = TEXT("Classifies boundary-connected ocean below EONFORM's fixed 0 m sea-level datum, exposes marine depth/shore/surface fields, and can form a shallow shelf plus restrained coastal erosion or deposition.");
	Descriptor.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Sea"), TEXT("Sea")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Depth"), TEXT("Depth")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Shore"), TEXT("Shore")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Surface"), TEXT("Surface")));
	Descriptor.Parameters.Add(Num(TEXT("ShoreWidthMeters"), TEXT("Shore Width (m)"), 120.0, 0.0, 100000.0, TEXT("Coast")));
	Descriptor.Parameters.Add(Num(TEXT("ShelfWidthMeters"), TEXT("Shelf Width (m)"), 1500.0, 0.0, 1000000.0, TEXT("Shelf")));
	Descriptor.Parameters.Add(Num(TEXT("ShelfDepthMeters"), TEXT("Shelf Depth (m)"), 120.0, 0.0, 20000.0, TEXT("Shelf")));
	Descriptor.Parameters.Add(Num(TEXT("CoastalErosionMeters"), TEXT("Coastal Erosion (m)"), 0.0, 0.0, 1000.0, TEXT("Coast")));
	Descriptor.Parameters.Add(Num(TEXT("BeachDepositionMeters"), TEXT("Beach Deposition (m)"), 0.0, 0.0, 1000.0, TEXT("Coast")));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluateSea);
}
