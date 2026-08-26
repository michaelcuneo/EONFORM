#include "EonformCryosphereNodes.h"

#include "EonformTerrainDerivedData.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformGlacierNode
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
		P.DefaultBool = Default;
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

	float SmoothStep01(double Value)
	{
		const float T = static_cast<float>(FMath::Clamp(Value, 0.0, 1.0));
		return T * T * (3.0f - 2.0f * T);
	}

	bool EvaluateGlacier(
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
			Error = TEXT("Glacier requires a valid terrain input 'Terrain'.");
			return false;
		}

		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		const FEonformScalarField* IncomingSnowfield = Dataset.FindScalarField(TEXT("SnowfieldDepth"));
		const FEonformScalarField* SourceBeforeContext = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!IncomingSnowfield || !IncomingSnowfield->IsValid() || !SourceBeforeContext)
		{
			Error = TEXT("Glacier requires SnowfieldDepth and Height from an upstream Snowfield process.");
			return false;
		}
		const FEonformScalarField SnowfieldInput = *IncomingSnowfield;
		FEonformScalarField TemperatureInput;
		const FEonformScalarField* IncomingTemperature = Dataset.FindScalarField(TEXT("TemperatureC"));
		const bool bHasTemperature = IncomingTemperature && IncomingTemperature->IsValid() && IncomingTemperature->Domain == SnowfieldInput.Domain;
		if (bHasTemperature) TemperatureInput = *IncomingTemperature;

		if (!FEonformTerrainDerivedData::EnsureContext(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;
		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		const FEonformScalarField* Concavity = Dataset.FindScalarField(EonformTerrainFieldNames::Concavity);
		if (!Source || !Concavity || SnowfieldInput.Domain != Source->Domain)
		{
			Error = TEXT("Glacier could not resolve compatible Height, SnowfieldDepth, and Concavity fields.");
			return false;
		}

		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		const FVector2d SpacingMeters = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Source->Domain.Dimensions, Source->Domain.GetCellSize());
		if (ElevationScaleMeters <= UE_DOUBLE_SMALL_NUMBER || SpacingMeters.X <= UE_DOUBLE_SMALL_NUMBER || SpacingMeters.Y <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("Glacier could not resolve physical terrain metrics.");
			return false;
		}

		const int32 W = Source->Domain.Dimensions.X;
		const int32 H = Source->Domain.Dimensions.Y;
		const int32 NumCells = W * H;
		const int32 Iterations = FMath::Clamp(FMath::RoundToInt(Node.GetNumber(TEXT("Iterations"), 20.0)), 1, 128);
		const double FirnDepthMeters = FMath::Max(Node.GetNumber(TEXT("FirnDepthMeters"), 0.6), 0.0);
		const double IceCompaction = FMath::Clamp(Node.GetNumber(TEXT("IceCompaction"), 0.72), 0.05, 1.0);
		const double FlowStrength = FMath::Clamp(Node.GetNumber(TEXT("FlowStrength"), 0.12), 0.0, 1.0);
		const double MinimumFlowSlopeDegrees = FMath::Clamp(Node.GetNumber(TEXT("MinimumFlowSlopeDegrees"), 1.5), 0.0, 45.0);
		const double MeltTemperatureC = Node.GetNumber(TEXT("MeltTemperatureC"), 0.0);
		const double MeltRateMetersPerC = FMath::Max(Node.GetNumber(TEXT("MeltRateMetersPerC"), 0.02), 0.0);
		const double ErosionStrength = FMath::Max(Node.GetNumber(TEXT("ErosionStrength"), 0.015), 0.0);
		const double MaximumErosionMeters = FMath::Max(Node.GetNumber(TEXT("MaximumErosionMeters"), 12.0), 0.0);
		const double ValleyPreference = FMath::Clamp(Node.GetNumber(TEXT("ValleyPreference"), 0.65), 0.0, 1.0);
		const double BaseTemperatureC = Node.GetNumber(TEXT("BaseTemperatureC"), 12.0);
		const double LapseRateCPerKm = Node.GetNumber(TEXT("LapseRateCPerKm"), 6.5);
		const bool bAffectHeight = Node.GetBool(TEXT("AffectHeight"), true);

		TArray<double> BedMeters;
		TArray<double> RemainingSnowMeters;
		TArray<double> IceMeters;
		TArray<double> ErosionMeters;
		TArray<double> MoraineMeters;
		TArray<double> TemperatureC;
		BedMeters.SetNumUninitialized(NumCells);
		RemainingSnowMeters.SetNumUninitialized(NumCells);
		IceMeters.SetNumUninitialized(NumCells);
		ErosionMeters.Init(0.0, NumCells);
		MoraineMeters.Init(0.0, NumCells);
		TemperatureC.SetNumUninitialized(NumCells);

		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 Index = Y * W + X;
				const double SnowDepth = FMath::Max(static_cast<double>(SnowfieldInput.AtInterior(X, Y)), 0.0);
				const double SurfaceMeters = static_cast<double>(Source->AtInterior(X, Y)) * ElevationScaleMeters;
				BedMeters[Index] = SurfaceMeters - SnowDepth;
				const double LocalTemperature = bHasTemperature
					? static_cast<double>(TemperatureInput.AtInterior(X, Y))
					: BaseTemperatureC - LapseRateCPerKm * (BedMeters[Index] / 1000.0);
				TemperatureC[Index] = LocalTemperature;

				const double FirnExcess = FMath::Max(SnowDepth - FirnDepthMeters, 0.0);
				const double ColdPersistence = 1.0 - static_cast<double>(SmoothStep01((LocalTemperature - MeltTemperatureC + 2.0) / 4.0));
				const double Valley = FMath::Clamp(static_cast<double>(Concavity->AtInterior(X, Y)), 0.0, 1.0);
				const double Formation = FMath::Lerp(1.0, 0.5 + 0.5 * Valley, ValleyPreference);
				IceMeters[Index] = FirnExcess * IceCompaction * ColdPersistence * Formation;
				RemainingSnowMeters[Index] = FMath::Max(SnowDepth - FirnExcess, 0.0);
			}
		}

		static const int32 DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
		static const int32 DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
		const double MinSpacing = FMath::Max(FMath::Min(SpacingMeters.X, SpacingMeters.Y), UE_DOUBLE_SMALL_NUMBER);
		const double MinimumGradient = FMath::Tan(FMath::DegreesToRadians(MinimumFlowSlopeDegrees));

		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			TArray<double> Delta;
			Delta.Init(0.0, NumCells);
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					const int32 Index = Y * W + X;
					const double Ice = IceMeters[Index];
					if (Ice <= UE_DOUBLE_SMALL_NUMBER) continue;

					const double CurrentSurface = BedMeters[Index] + Ice;
					int32 BestIndex = INDEX_NONE;
					double BestGradient = 0.0;
					for (int32 Direction = 0; Direction < 8; ++Direction)
					{
						const int32 NX = X + DX[Direction];
						const int32 NY = Y + DY[Direction];
						if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
						const int32 NIndex = NY * W + NX;
						const double Distance = (DX[Direction] != 0 && DY[Direction] != 0) ? MinSpacing * 1.41421356237 : MinSpacing;
						const double NeighborSurface = BedMeters[NIndex] + IceMeters[NIndex];
						const double Gradient = (CurrentSurface - NeighborSurface) / FMath::Max(Distance, UE_DOUBLE_SMALL_NUMBER);
						if (Gradient > BestGradient)
						{
							BestGradient = Gradient;
							BestIndex = NIndex;
						}
					}

					if (BestIndex == INDEX_NONE || BestGradient <= MinimumGradient) continue;
					const double Valley = FMath::Clamp(static_cast<double>(Concavity->AtInterior(X, Y)), 0.0, 1.0);
					const double FlowBias = FMath::Lerp(1.0, 0.65 + 0.7 * Valley, ValleyPreference);
					const double GradientExcess = FMath::Clamp((BestGradient - MinimumGradient) / FMath::Max(BestGradient, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
					const double Transfer = Ice * FlowStrength * GradientExcess * FlowBias;
					Delta[Index] -= Transfer;
					Delta[BestIndex] += Transfer;

					const double Erode = FMath::Min(
						ErosionStrength * Ice * FMath::Clamp(BestGradient, 0.0, 1.0) / static_cast<double>(Iterations),
						FMath::Max(MaximumErosionMeters - ErosionMeters[Index], 0.0));
					ErosionMeters[Index] += Erode;
					BedMeters[Index] -= Erode;
				}
			}

			for (int32 Index = 0; Index < NumCells; ++Index)
			{
				IceMeters[Index] = FMath::Max(IceMeters[Index] + Delta[Index], 0.0);
			}
		}

		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			const double Warmth = FMath::Max(TemperatureC[Index] - MeltTemperatureC, 0.0);
			const double Melt = FMath::Min(IceMeters[Index], Warmth * MeltRateMetersPerC);
			if (Melt > 0.0)
			{
				IceMeters[Index] -= Melt;
				MoraineMeters[Index] += Melt * 0.08;
			}
		}

		FEonformScalarField Height = *Source;
		FEonformScalarField Glacier = MakeScalar(Source->Domain, TEXT("Glacier"));
		FEonformScalarField IceDepth = MakeScalar(Source->Domain, TEXT("IceDepth"), EEonformFieldUnit::Meters);
		FEonformScalarField GlacialErosion = MakeScalar(Source->Domain, TEXT("GlacialErosion"), EEonformFieldUnit::Meters);
		FEonformScalarField Moraine = MakeScalar(Source->Domain, TEXT("Moraine"), EEonformFieldUnit::Meters);
		FEonformScalarField IceFlow = MakeScalar(Source->Domain, TEXT("IceFlow"));

		double MaximumIce = 0.0;
		for (const double Value : IceMeters) MaximumIce = FMath::Max(MaximumIce, Value);
		const double IceNormalizer = FMath::Max(MaximumIce, 0.001);

		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 Index = Y * W + X;
				Glacier.AtInterior(X, Y) = static_cast<float>(FMath::Clamp(IceMeters[Index] / IceNormalizer, 0.0, 1.0));
				IceDepth.AtInterior(X, Y) = static_cast<float>(IceMeters[Index]);
				GlacialErosion.AtInterior(X, Y) = static_cast<float>(ErosionMeters[Index]);
				Moraine.AtInterior(X, Y) = static_cast<float>(MoraineMeters[Index]);

				double MaxLocalDrop = 0.0;
				const double CurrentSurface = BedMeters[Index] + IceMeters[Index];
				for (int32 Direction = 0; Direction < 8; ++Direction)
				{
					const int32 NX = X + DX[Direction];
					const int32 NY = Y + DY[Direction];
					if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
					const int32 NIndex = NY * W + NX;
					MaxLocalDrop = FMath::Max(MaxLocalDrop, CurrentSurface - (BedMeters[NIndex] + IceMeters[NIndex]));
				}
				IceFlow.AtInterior(X, Y) = static_cast<float>(FMath::Clamp(MaxLocalDrop / FMath::Max(MinSpacing, 1.0), 0.0, 1.0));

				if (bAffectHeight)
				{
					const double SurfaceCover = RemainingSnowMeters[Index] + IceMeters[Index];
					Height.AtInterior(X, Y) = FMath::Clamp(static_cast<float>((BedMeters[Index] + SurfaceCover) / ElevationScaleMeters), -1.0f, 1.0f);
				}
			}
		}

		if (bAffectHeight)
		{
			Height.Descriptor.Name = EonformTerrainFieldNames::Height;
			if (!Dataset.SetScalarField(MoveTemp(Height)))
			{
				Error = TEXT("Glacier could not publish glacially modified Height.");
				return false;
			}
		}

		FEonformScalarField GlacierOutput = Glacier;
		FEonformScalarField DepthOutput = IceDepth;
		FEonformScalarField ErosionOutput = GlacialErosion;
		FEonformScalarField MoraineOutput = Moraine;
		FEonformScalarField FlowOutput = IceFlow;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Glacier))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(IceDepth))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(GlacialErosion))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Moraine))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(IceFlow)))
		{
			Error = TEXT("Glacier could not publish ice and glacial process fields.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Glacier"), FEonformTerrainValue::MakeScalarField(MoveTemp(GlacierOutput)));
		Out.Outputs.Add(TEXT("Depth"), FEonformTerrainValue::MakeScalarField(MoveTemp(DepthOutput)));
		Out.Outputs.Add(TEXT("Erosion"), FEonformTerrainValue::MakeScalarField(MoveTemp(ErosionOutput)));
		Out.Outputs.Add(TEXT("Moraine"), FEonformTerrainValue::MakeScalarField(MoveTemp(MoraineOutput)));
		Out.Outputs.Add(TEXT("Flow"), FEonformTerrainValue::MakeScalarField(MoveTemp(FlowOutput)));
		Error.Reset();
		return true;
	}
}

void RegisterEonformGlacierNode()
{
	using namespace EonformGlacierNode;
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Glacier;
	Descriptor.DisplayName = TEXT("Glacier");
	Descriptor.Category = TEXT("Simulate");
	Descriptor.Description = TEXT("Converts persistent deep snowfields into compacted ice, moves ice downslope with valley preference, erodes the underlying bed under moving ice, and exposes moraine and ice-flow state.");
	Descriptor.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Glacier"), TEXT("Glacier")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Depth"), TEXT("Ice Depth")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Erosion"), TEXT("Erosion")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Moraine"), TEXT("Moraine")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Flow"), TEXT("Ice Flow")));
	Descriptor.Parameters.Add(Num(TEXT("Iterations"), TEXT("Iterations"), 20.0, 1.0, 128.0, TEXT("Flow")));
	Descriptor.Parameters.Add(Num(TEXT("FirnDepthMeters"), TEXT("Firn Depth (m)"), 0.6, 0.0, 100.0, TEXT("Formation")));
	Descriptor.Parameters.Add(Num(TEXT("IceCompaction"), TEXT("Ice Compaction"), 0.72, 0.05, 1.0, TEXT("Formation")));
	Descriptor.Parameters.Add(Num(TEXT("FlowStrength"), TEXT("Flow Strength"), 0.12, 0.0, 1.0, TEXT("Flow")));
	Descriptor.Parameters.Add(Num(TEXT("MinimumFlowSlopeDegrees"), TEXT("Minimum Flow Slope (deg)"), 1.5, 0.0, 45.0, TEXT("Flow")));
	Descriptor.Parameters.Add(Num(TEXT("MeltTemperatureC"), TEXT("Melt Temperature (C)"), 0.0, -20.0, 20.0, TEXT("Climate")));
	Descriptor.Parameters.Add(Num(TEXT("MeltRateMetersPerC"), TEXT("Melt Rate (m/C)"), 0.02, 0.0, 10.0, TEXT("Climate")));
	Descriptor.Parameters.Add(Num(TEXT("ErosionStrength"), TEXT("Erosion Strength"), 0.015, 0.0, 10.0, TEXT("Erosion")));
	Descriptor.Parameters.Add(Num(TEXT("MaximumErosionMeters"), TEXT("Maximum Erosion (m)"), 12.0, 0.0, 10000.0, TEXT("Erosion")));
	Descriptor.Parameters.Add(Num(TEXT("ValleyPreference"), TEXT("Valley Preference"), 0.65, 0.0, 1.0, TEXT("Flow")));
	Descriptor.Parameters.Add(Num(TEXT("BaseTemperatureC"), TEXT("Base Temperature (C)"), 12.0, -80.0, 60.0, TEXT("Climate")));
	Descriptor.Parameters.Add(Num(TEXT("LapseRateCPerKm"), TEXT("Lapse Rate (C/km)"), 6.5, 0.0, 20.0, TEXT("Climate")));
	Descriptor.Parameters.Add(Bool(TEXT("AffectHeight"), TEXT("Affect Height"), true, TEXT("Output")));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(Descriptor.Type, EvaluateGlacier);
}
