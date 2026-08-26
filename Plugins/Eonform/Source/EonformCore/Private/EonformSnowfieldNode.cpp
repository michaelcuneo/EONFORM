#include "EonformCryosphereNodes.h"

#include "EonformTerrainDerivedData.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformSnowfieldNode
{
	FEonformTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* DisplayName)
	{
		FEonformTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.DataType = TEXT("Terrain");
		return P;
	}

	FEonformTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* DisplayName)
	{
		FEonformTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.DataType = TEXT("ScalarField");
		return P;
	}

	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		P.Group = Group;
		return P;
	}

	FEonformTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		P.Group = Group;
		return P;
	}

	FEonformScalarField MakeScalar(const FEonformGridDomain& Domain, FName Name, EEonformFieldUnit Unit = EEonformFieldUnit::Normalized)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	bool EvaluateSnowfield(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Snowfield requires a valid terrain input 'Terrain'.");
			return false;
		}

		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		const FEonformScalarField* IncomingSnow = Dataset.FindScalarField(TEXT("Snow"));
		const FEonformScalarField* IncomingDepth = Dataset.FindScalarField(TEXT("SnowDepth"));
		const FEonformScalarField* Temperature = Dataset.FindScalarField(TEXT("TemperatureC"));
		const FEonformScalarField* SourceBeforeContext = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!IncomingSnow || !IncomingDepth || !Temperature || !SourceBeforeContext
			|| !IncomingSnow->IsValid() || !IncomingDepth->IsValid() || !Temperature->IsValid())
		{
			Error = TEXT("Snowfield requires Snow, SnowDepth, TemperatureC, and Height fields from an upstream Snow process.");
			return false;
		}

		const FEonformScalarField SnowInput = *IncomingSnow;
		const FEonformScalarField DepthInput = *IncomingDepth;
		const FEonformScalarField TemperatureInput = *Temperature;

		if (!FEonformTerrainDerivedData::EnsureContext(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;
		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		const FEonformScalarField* Concavity = Dataset.FindScalarField(EonformTerrainFieldNames::Concavity);
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

		FEonformScalarField Height = *Source;
		FEonformScalarField Snowfield = MakeScalar(Source->Domain, TEXT("Snowfield"));
		FEonformScalarField SnowfieldDepth = MakeScalar(Source->Domain, TEXT("SnowfieldDepth"), EEonformFieldUnit::Meters);
		FEonformScalarField Drift = MakeScalar(Source->Domain, TEXT("SnowDrift"), EEonformFieldUnit::Meters);
		FEonformScalarField Melt = MakeScalar(Source->Domain, TEXT("SnowMelt"), EEonformFieldUnit::Meters);

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
			Height.Descriptor.Name = EonformTerrainFieldNames::Height;
			if (!Dataset.SetScalarField(MoveTemp(Height)))
			{
				Error = TEXT("Snowfield could not publish redistributed snow Height.");
				return false;
			}
		}

		// Temperature is a height-derived Snow semantic, so replacing Height above
		// correctly invalidates the upstream copy. Snowfield still represents the
		// same atmospheric event, however, and Glacier must inherit that climate
		// rather than silently reverting to its standalone default temperature.
		FEonformScalarField TemperatureAfterSnowfield = TemperatureInput;
		TemperatureAfterSnowfield.Descriptor.Name = TEXT("TemperatureC");

		FEonformScalarField SnowfieldOutput = Snowfield;
		FEonformScalarField DepthOutput = SnowfieldDepth;
		FEonformScalarField DriftOutput = Drift;
		FEonformScalarField MeltOutput = Melt;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(TemperatureAfterSnowfield))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Snowfield))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(SnowfieldDepth))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Drift))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Melt)))
		{
			Error = TEXT("Snowfield could not publish redistributed snow and climate fields.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Snowfield"), FEonformTerrainValue::MakeScalarField(MoveTemp(SnowfieldOutput)));
		Out.Outputs.Add(TEXT("Depth"), FEonformTerrainValue::MakeScalarField(MoveTemp(DepthOutput)));
		Out.Outputs.Add(TEXT("Drift"), FEonformTerrainValue::MakeScalarField(MoveTemp(DriftOutput)));
		Out.Outputs.Add(TEXT("Melt"), FEonformTerrainValue::MakeScalarField(MoveTemp(MeltOutput)));
		Error.Reset();
		return true;
	}
}

void RegisterEonformSnowfieldNode()
{
	using namespace EonformSnowfieldNode;
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Snowfield;
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
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(Descriptor.Type, EvaluateSnowfield);
}
