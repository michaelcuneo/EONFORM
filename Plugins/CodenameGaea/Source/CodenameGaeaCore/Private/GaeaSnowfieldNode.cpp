#include "GaeaCryosphereNodes.h"

#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaSnowfieldNode
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

	FGaeaTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
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
		P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		P.Group = Group;
		return P;
	}

	FGaeaScalarField MakeScalar(const FGaeaGridDomain& Domain, FName Name, EGaeaFieldUnit Unit = EGaeaFieldUnit::Normalized)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	bool EvaluateSnowfield(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Snowfield requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		const FGaeaScalarField* IncomingSnow = Dataset.FindScalarField(TEXT("Snow"));
		const FGaeaScalarField* IncomingDepth = Dataset.FindScalarField(TEXT("SnowDepth"));
		const FGaeaScalarField* Temperature = Dataset.FindScalarField(TEXT("TemperatureC"));
		const FGaeaScalarField* SourceBeforeContext = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!IncomingSnow || !IncomingDepth || !Temperature || !SourceBeforeContext
			|| !IncomingSnow->IsValid() || !IncomingDepth->IsValid() || !Temperature->IsValid())
		{
			Error = TEXT("Snowfield requires Snow, SnowDepth, TemperatureC, and Height fields from an upstream Snow process.");
			return false;
		}

		const FGaeaScalarField SnowInput = *IncomingSnow;
		const FGaeaScalarField DepthInput = *IncomingDepth;
		const FGaeaScalarField TemperatureInput = *Temperature;

		if (!FGaeaTerrainDerivedData::EnsureContext(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;
		const FGaeaScalarField* Source = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* Concavity = Dataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
		if (!Source || !Concavity)
		{
			Error = TEXT("Snowfield could not resolve Height and Concavity fields.");
			return false;
		}
		if (DepthInput.Domain != Source->Domain || TemperatureInput.Domain != Source->Domain)
		{
			Error = TEXT("Snowfield input cryosphere fields do not match the terrain domain.");
			return false;
		}

		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		const FVector2d SpacingMeters = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Source->Domain.Dimensions, Source->Domain.GetCellSize());
		if (ElevationScaleMeters <= UE_DOUBLE_SMALL_NUMBER || SpacingMeters.X <= UE_DOUBLE_SMALL_NUMBER || SpacingMeters.Y <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("Snowfield could not resolve physical terrain metrics.");
			return false;
		}

		const int32 W = Source->Domain.Dimensions.X;
		const int32 H = Source->Domain.Dimensions.Y;
		const int32 NumCells = W * H;
		const int32 Iterations = FMath::Clamp(FMath::RoundToInt(Node.GetNumber(TEXT("Iterations"), 12.0)), 1, 128);
		const double TransportStrength = FMath::Clamp(Node.GetNumber(TEXT("TransportStrength"), 0.32), 0.0, 1.0);
		const double TransportSlopeDegrees = FMath::Clamp(Node.GetNumber(TEXT("TransportSlopeDegrees"), 18.0), 0.0, 80.0);
		const double MeltTemperatureC = Node.GetNumber(TEXT("MeltTemperatureC"), 0.5);
		const double MeltRateMetersPerC = FMath::Max(Node.GetNumber(TEXT("MeltRateMetersPerC"), 0.035), 0.0);
		const double Compaction = FMath::Clamp(Node.GetNumber(TEXT("Compaction"), 0.08), 0.0, 0.95);
		const double ShelterRetention = FMath::Clamp(Node.GetNumber(TEXT("ShelterRetention"), 0.55), 0.0, 1.0);
		const bool bAffectHeight = Node.GetBool(TEXT("AffectHeight"), true);

		TArray<double> BedMeters;
		TArray<double> DepthMeters;
		TArray<double> OriginalDepthMeters;
		TArray<double> MeltMeters;
		BedMeters.SetNumUninitialized(NumCells);
		DepthMeters.SetNumUninitialized(NumCells);
		OriginalDepthMeters.SetNumUninitialized(NumCells);
		MeltMeters.Init(0.0, NumCells);

		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 Index = Y * W + X;
				const double ExistingDepth = FMath::Max(static_cast<double>(DepthInput.AtInterior(X, Y)), 0.0);
				const double SurfaceMeters = static_cast<double>(Source->AtInterior(X, Y)) * ElevationScaleMeters;
				BedMeters[Index] = SurfaceMeters - ExistingDepth;
				OriginalDepthMeters[Index] = ExistingDepth;
				DepthMeters[Index] = ExistingDepth * (1.0 - Compaction);

				const double Warmth = FMath::Max(static_cast<double>(TemperatureInput.AtInterior(X, Y)) - MeltTemperatureC, 0.0);
				const double Melt = FMath::Min(DepthMeters[Index], Warmth * MeltRateMetersPerC);
				DepthMeters[Index] -= Melt;
				MeltMeters[Index] += Melt;
			}
		}

		static const int32 DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
		static const int32 DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
		const double MinSpacing = FMath::Max(FMath::Min(SpacingMeters.X, SpacingMeters.Y), UE_DOUBLE_SMALL_NUMBER);
		const double TransportGradient = FMath::Tan(FMath::DegreesToRadians(TransportSlopeDegrees));

		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			TArray<double> Delta;
			Delta.Init(0.0, NumCells);

			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					const int32 Index = Y * W + X;
					const double Available = DepthMeters[Index];
					if (Available <= UE_DOUBLE_SMALL_NUMBER) continue;

					const double CurrentSurface = BedMeters[Index] + Available;
					int32 BestIndex = INDEX_NONE;
					double BestGradient = 0.0;
					for (int32 Direction = 0; Direction < 8; ++Direction)
					{
						const int32 NX = X + DX[Direction];
						const int32 NY = Y + DY[Direction];
						if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
						const int32 NIndex = NY * W + NX;
						const double Distance = (DX[Direction] != 0 && DY[Direction] != 0) ? MinSpacing * 1.41421356237 : MinSpacing;
						const double NeighborSurface = BedMeters[NIndex] + DepthMeters[NIndex];
						const double Gradient = (CurrentSurface - NeighborSurface) / FMath::Max(Distance, UE_DOUBLE_SMALL_NUMBER);
						if (Gradient > BestGradient)
						{
							BestGradient = Gradient;
							BestIndex = NIndex;
						}
					}

					if (BestIndex == INDEX_NONE || BestGradient <= TransportGradient) continue;
					const double Shelter = FMath::Clamp(static_cast<double>(Concavity->AtInterior(X, Y)), 0.0, 1.0);
					const double Retention = FMath::Clamp(Shelter * ShelterRetention, 0.0, 0.95);
					const double Excess = FMath::Clamp((BestGradient - TransportGradient) / FMath::Max(BestGradient, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
					const double Transfer = Available * TransportStrength * Excess * (1.0 - Retention);
					Delta[Index] -= Transfer;
					Delta[BestIndex] += Transfer;
				}
			}

			for (int32 Index = 0; Index < NumCells; ++Index)
			{
				DepthMeters[Index] = FMath::Max(DepthMeters[Index] + Delta[Index], 0.0);
			}
		}

		FGaeaScalarField Height = *Source;
		FGaeaScalarField Snowfield = MakeScalar(Source->Domain, TEXT("Snowfield"));
		FGaeaScalarField SnowfieldDepth = MakeScalar(Source->Domain, TEXT("SnowfieldDepth"), EGaeaFieldUnit::Meters);
		FGaeaScalarField Drift = MakeScalar(Source->Domain, TEXT("SnowDrift"), EGaeaFieldUnit::Meters);
		FGaeaScalarField Melt = MakeScalar(Source->Domain, TEXT("SnowMelt"), EGaeaFieldUnit::Meters);

		double MaximumDepth = 0.0;
		for (const double Value : DepthMeters) MaximumDepth = FMath::Max(MaximumDepth, Value);
		const double DepthNormalizer = FMath::Max(MaximumDepth, 0.001);

		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 Index = Y * W + X;
				const double Depth = DepthMeters[Index];
				Snowfield.AtInterior(X, Y) = static_cast<float>(FMath::Clamp(Depth / DepthNormalizer, 0.0, 1.0));
				SnowfieldDepth.AtInterior(X, Y) = static_cast<float>(Depth);
				Drift.AtInterior(X, Y) = static_cast<float>(FMath::Max(Depth - OriginalDepthMeters[Index], 0.0));
				Melt.AtInterior(X, Y) = static_cast<float>(MeltMeters[Index]);
				if (bAffectHeight)
				{
					Height.AtInterior(X, Y) = FMath::Clamp(static_cast<float>((BedMeters[Index] + Depth) / ElevationScaleMeters), -1.0f, 1.0f);
				}
			}
		}

		if (bAffectHeight)
		{
			Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
			if (!Dataset.SetScalarField(MoveTemp(Height)))
			{
				Error = TEXT("Snowfield could not publish redistributed snow Height.");
				return false;
			}
		}

		FGaeaScalarField SnowfieldOutput = Snowfield;
		FGaeaScalarField DepthOutput = SnowfieldDepth;
		FGaeaScalarField DriftOutput = Drift;
		FGaeaScalarField MeltOutput = Melt;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Snowfield))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(SnowfieldDepth))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Drift))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Melt)))
		{
			Error = TEXT("Snowfield could not publish redistributed snow fields.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Snowfield"), FGaeaTerrainValue::MakeScalarField(MoveTemp(SnowfieldOutput)));
		Out.Outputs.Add(TEXT("Depth"), FGaeaTerrainValue::MakeScalarField(MoveTemp(DepthOutput)));
		Out.Outputs.Add(TEXT("Drift"), FGaeaTerrainValue::MakeScalarField(MoveTemp(DriftOutput)));
		Out.Outputs.Add(TEXT("Melt"), FGaeaTerrainValue::MakeScalarField(MoveTemp(MeltOutput)));
		Error.Reset();
		return true;
	}
}

void RegisterGaeaSnowfieldNode()
{
	using namespace GaeaSnowfieldNode;
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Snowfield;
	Descriptor.DisplayName = TEXT("Snowfield");
	Descriptor.Category = TEXT("Simulate");
	Descriptor.Description = TEXT("Redistributes an upstream physical snowpack under gravity, terrain shelter, compaction, and temperature-driven melt, conserving transported snow mass across the terrain surface.");
	Descriptor.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Snowfield"), TEXT("Snowfield")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Depth"), TEXT("Depth")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Drift"), TEXT("Drift")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Melt"), TEXT("Melt")));
	Descriptor.Parameters.Add(Num(TEXT("Iterations"), TEXT("Iterations"), 12.0, 1.0, 128.0, TEXT("Transport")));
	Descriptor.Parameters.Add(Num(TEXT("TransportStrength"), TEXT("Transport Strength"), 0.32, 0.0, 1.0, TEXT("Transport")));
	Descriptor.Parameters.Add(Num(TEXT("TransportSlopeDegrees"), TEXT("Transport Slope (deg)"), 18.0, 0.0, 80.0, TEXT("Transport")));
	Descriptor.Parameters.Add(Num(TEXT("MeltTemperatureC"), TEXT("Melt Temperature (C)"), 0.5, -20.0, 20.0, TEXT("Melt")));
	Descriptor.Parameters.Add(Num(TEXT("MeltRateMetersPerC"), TEXT("Melt Rate (m/C)"), 0.035, 0.0, 10.0, TEXT("Melt")));
	Descriptor.Parameters.Add(Num(TEXT("Compaction"), TEXT("Compaction"), 0.08, 0.0, 0.95, TEXT("Snowpack")));
	Descriptor.Parameters.Add(Num(TEXT("ShelterRetention"), TEXT("Shelter Retention"), 0.55, 0.0, 1.0, TEXT("Snowpack")));
	Descriptor.Parameters.Add(Bool(TEXT("AffectHeight"), TEXT("Affect Height"), true, TEXT("Output")));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluateSnowfield);
}
