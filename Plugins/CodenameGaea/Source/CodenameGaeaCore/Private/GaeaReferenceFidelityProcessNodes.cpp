#include "GaeaReferenceFidelityProcessNodes.h"

#include "GaeaHydraulicErosion.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainPhysicalMetrics.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaReferenceFidelityProcess
{
	FGaeaTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FGaeaTerrainPortDescriptor P; P.Name = Name; P.DisplayName = Label; P.DataType = Type; return P;
	}
	FGaeaTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Number; P.DefaultNumber = Default; P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; if (Group) P.Group = Group; return P;
	}
	FGaeaTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Integer; P.DefaultInteger = Default; P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max); if (Group) P.Group = Group; return P;
	}
	FGaeaTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Boolean; P.DefaultBoolean = Default; if (Group) P.Group = Group; return P;
	}
	FGaeaTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Values, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Name; P.DefaultName = Default; for (const FName V : Values) P.NameOptions.Add(V); if (Group) P.Group = Group; return P;
	}

	const FGaeaTerrainValue* TerrainInput(const FGaeaTerrainNodeInputs& Inputs)
	{
		const FGaeaTerrainValue* const* P = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* V = P ? *P : nullptr;
		return V && V->Type == EGaeaTerrainValueType::Terrain && V->IsValid() ? V : nullptr;
	}
	const FGaeaScalarField* ScalarInput(const FGaeaTerrainNodeInputs& Inputs, FName Name)
	{
		const FGaeaTerrainValue* const* P = Inputs.Find(Name);
		const FGaeaTerrainValue* V = P ? *P : nullptr;
		if (!V || !V->IsValid()) return nullptr;
		if (V->Type == EGaeaTerrainValueType::ScalarField) return &V->ScalarField;
		if (V->Type == EGaeaTerrainValueType::Terrain) return V->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		return nullptr;
	}

	FGaeaScalarField MakeScalar(const FGaeaGridDomain& Domain, FName Name)
	{
		FGaeaFieldDescriptor D; D.Name = Name; D.Unit = EGaeaFieldUnit::Normalized; D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField F; F.Initialize(Domain, D, 0.0f); return F;
	}

	float EffectiveSolverHeightScale(const FGaeaScalarField& Height, float LegacyHeightScale, const FGaeaTerrainEvaluationContext& Context)
	{
		const FVector2d DomainCell = Height.Domain.GetCellSize();
		const double DomainRepresentative = FMath::Max(FMath::Min(FMath::Abs(DomainCell.X), FMath::Abs(DomainCell.Y)), UE_DOUBLE_SMALL_NUMBER);
		const double PhysicalSpacing = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(Height.Domain.Dimensions, DomainCell);
		const double PhysicalElevation = Context.PhysicalMetrics.ResolveElevationScaleMeters(LegacyHeightScale);
		return static_cast<float>(FMath::Max(PhysicalElevation / FMath::Max(PhysicalSpacing, UE_DOUBLE_SMALL_NUMBER) * DomainRepresentative, 1.0));
	}

	void ConfigurePhysical(const FGaeaScalarField& Height, float HeightScale, const FGaeaTerrainEvaluationContext& Context, FGaeaHydraulicErosionSettings& Settings)
	{
		Settings.PhysicalSampleSpacingMeters = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(Height.Domain.Dimensions, Height.Domain.GetCellSize());
		Settings.PhysicalElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(HeightScale);
		Settings.bAdvancedFlowSolver = true;
	}

	float PhysicalScaleToSolver(double Meters, double ReferenceMeters)
	{
		return FMath::Clamp(static_cast<float>(Meters / FMath::Max(ReferenceMeters, 1.0)), 0.25f, 8.0f);
	}

	bool PublishResult(
		FGaeaTerrainDataset&& Dataset,
		float HeightScale,
		FGaeaHydraulicErosionResult&& Result,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error,
		const FGaeaTerrainEvaluationContext& Context)
	{
		FGaeaScalarField Wear = Result.Wear;
		FGaeaScalarField Deposits = Result.Deposits;
		FGaeaScalarField Flow = Result.Flow;
		if (!Dataset.SetScalarField(MoveTemp(Result.Height))
			|| !Dataset.SetScalarField(MoveTemp(Result.Wear))
			|| !Dataset.SetScalarField(MoveTemp(Result.Deposits))
			|| !Dataset.SetScalarField(MoveTemp(Result.Flow)))
		{
			Error = TEXT("Hydraulic process could not publish simulation fields.");
			return false;
		}
		if (!FGaeaTerrainDerivedData::EnsureContext(Dataset, FMath::Max(HeightScale, 1.0f), Context.PhysicalMetrics, &Error)) return false;
		FGaeaTerrainValue Terrain = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Terrain.IsValid()) { Error = TEXT("Hydraulic process produced invalid terrain output."); return false; }
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Terrain));
		Out.Outputs.Add(TEXT("Wear"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Wear)));
		Out.Outputs.Add(TEXT("Deposits"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Deposits)));
		Out.Outputs.Add(TEXT("Flow"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Flow)));
		return true;
	}

	bool BuildSelectiveArea(
		const FGaeaTerrainNode& Node,
		const FGaeaScalarField& Height,
		const FGaeaTerrainEvaluationContext& Context,
		const FGaeaScalarField* ExplicitArea,
		FGaeaScalarField& Out)
	{
		const FName BiasType = Node.GetName(TEXT("BiasType"), TEXT("Altitude"));
		const float Bias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Bias"), 0.5)), 0.0f, 1.0f);
		const bool bReverse = Node.GetBool(TEXT("Reverse"), false);
		const double ElevationMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(1.0f);
		const FVector2d Spacing = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Height.Domain.Dimensions, Height.Domain.GetCellSize());
		Out = MakeScalar(Height.Domain, TEXT("ErosionSelectiveArea"));
		for (int32 Y = 0; Y < Height.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height.Domain.Dimensions.X; ++X)
			{
				float V = 0.0f;
				if (BiasType == TEXT("Slope"))
				{
					const int32 XL = FMath::Max(0, X - 1), XR = FMath::Min(Height.Domain.Dimensions.X - 1, X + 1);
					const int32 YD = FMath::Max(0, Y - 1), YU = FMath::Min(Height.Domain.Dimensions.Y - 1, Y + 1);
					const float GX = static_cast<float>((Height.AtInterior(XR, Y) - Height.AtInterior(XL, Y)) * ElevationMeters / FMath::Max((XR - XL) * Spacing.X, UE_DOUBLE_SMALL_NUMBER));
					const float GY = static_cast<float>((Height.AtInterior(X, YU) - Height.AtInterior(X, YD)) * ElevationMeters / FMath::Max((YU - YD) * Spacing.Y, UE_DOUBLE_SMALL_NUMBER));
					V = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(GX * GX + GY * GY))) / 90.0f, 0.0f, 1.0f);
				}
				else
				{
					V = FMath::Clamp(Height.AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
				}
				V = FMath::Clamp((V - Bias) / FMath::Max(1.0f - Bias, UE_SMALL_NUMBER), 0.0f, 1.0f);
				if (bReverse) V = 1.0f - V;
				if (ExplicitArea) V *= FMath::Clamp(ExplicitArea->AtInterior(X, Y), 0.0f, 1.0f);
				Out.AtInterior(X, Y) = V;
			}
		}
		return true;
	}

	bool EvaluateErosion(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* Input = TerrainInput(Inputs);
		if (!Input) { Error = TEXT("Erosion requires Terrain input."); return false; }
		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		FGaeaTerrainDerivedDataSettings Derived;
		if (!FGaeaTerrainDerivedData::EnsureHydraulicInputs(Dataset, Input->HeightScale, Context.PhysicalMetrics, Derived, &Error)) return false;
		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid()) { Error = TEXT("Erosion input has no valid Height."); return false; }

		FGaeaHydraulicErosionSettings S;
		S.Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Duration"), 24)), 1, 4096);
		S.RockSoftness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RockSoftness"), 0.0)), 0.0f, 1.0f);
		S.Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 1.0)), 0.0f, 4.0f);
		S.Downcutting = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Downcutting"), 0.5)), 0.0f, 2.0f);
		S.Inhibition = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Inhibition"), 0.0)), 0.0f, 1.0f);
		S.BaseLevel = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("BaseLevel"), -1.0)), -1.0f, 1.0f);
		const double FeatureScaleMeters = FMath::Clamp(Node.GetNumber(TEXT("FeatureScale"), 2000.0), 1.0, 20000.0);
		const bool bRealScale = Node.GetBool(TEXT("RealScale"), true);
		const double TerrainScale = FMath::Clamp(Node.GetNumber(TEXT("TerrainScale"), 1.0), 0.01, 100.0);
		const float Verticality = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Verticality"), 1.0)), 0.01f, 10.0f);
		const double EffectiveFeatureMeters = bRealScale ? FeatureScaleMeters : FeatureScaleMeters * TerrainScale;
		S.FeatureScale = PhysicalScaleToSolver(EffectiveFeatureMeters, 250.0);
		S.Strength *= Verticality;
		S.Debris = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Debris"), 0.5)), 0.0f, 1.0f);
		S.Volume = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Volume"), 1.0)), 0.0f, 4.0f);
		S.SedimentRemoval = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SedimentRemoval"), 0.0)), 0.0f, 1.0f);
		S.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		S.bAggressiveMode = Node.GetBool(TEXT("AggressiveMode"), false);
		S.bDeterministic = Node.GetBool(TEXT("Deterministic"), true);
		const FName AreaEffect = Node.GetName(TEXT("AreaEffect"), TEXT("None"));
		if (AreaEffect == TEXT("ErosionStrength") || AreaEffect == TEXT("Erosion Strength")) S.SelectiveProcessing = TEXT("ErosionStrength");
		else if (AreaEffect == TEXT("RockSoftness") || AreaEffect == TEXT("Rock Softness")) S.SelectiveProcessing = TEXT("RockSoftness");
		else if (AreaEffect == TEXT("PrecipitationAmount") || AreaEffect == TEXT("Precipitation Amount")) S.SelectiveProcessing = TEXT("Precipitation");
		ConfigurePhysical(*Height, Input->HeightScale, Context, S);

		FGaeaScalarField Area;
		const FGaeaScalarField* EffectiveArea = nullptr;
		if (S.SelectiveProcessing != TEXT("None"))
		{
			if (!BuildSelectiveArea(Node, *Height, Context, ScalarInput(Inputs, TEXT("Area")), Area)) return false;
			EffectiveArea = &Area;
		}

		FGaeaHydraulicErosionResult R;
		if (!FGaeaHydraulicErosion::Evaluate(
			*Height,
			EffectiveSolverHeightScale(*Height, Input->HeightScale, Context),
			S,
			R,
			Dataset.FindScalarField(GaeaTerrainFieldNames::Rainfall),
			Dataset.FindScalarField(GaeaTerrainFieldNames::HydraulicErosion),
			Dataset.FindScalarField(GaeaTerrainFieldNames::Deposition),
			Dataset.FindScalarField(GaeaTerrainFieldNames::Evaporation),
			Dataset.FindScalarField(GaeaTerrainFieldNames::RockHardness),
			Dataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth),
			EffectiveArea))
		{
			Error = TEXT("Erosion solver failed."); return false;
		}
		return PublishResult(MoveTemp(Dataset), Input->HeightScale, MoveTemp(R), Out, Error, Context);
	}

	bool EvaluateErosion2(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* Input = TerrainInput(Inputs);
		if (!Input) { Error = TEXT("Erosion2 requires Terrain input."); return false; }
		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		FGaeaTerrainDerivedDataSettings Derived;
		if (!FGaeaTerrainDerivedData::EnsureHydraulicInputs(Dataset, Input->HeightScale, Context.PhysicalMetrics, Derived, &Error)) return false;
		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid()) { Error = TEXT("Erosion2 input has no valid Height."); return false; }

		const int32 Duration = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Duration"), 64)), 1, 512);
		const float Downcutting = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Downcutting"), 0.35)), 0.0f, 1.0f);
		const double ErosionScale = FMath::Clamp(Node.GetNumber(TEXT("ErosionScale"), 500.0), 1.0, 20000.0);
		const float Suspended = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SuspendedLoad"), 0.5)), 0.0f, 1.0f);
		const float SuspendedAngle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SuspendedDischargeAngle"), 18.0)), 0.0f, 80.0f);
		const float Bed = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("BedLoad"), 0.45)), 0.0f, 1.0f);
		const float BedAngle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("BedDischargeAngle"), 28.0)), 0.0f, 80.0f);
		const float Coarse = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("CoarseSediments"), 0.3)), 0.0f, 1.0f);
		const float CoarseAngle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("CoarseDischargeAngle"), 38.0)), 0.0f, 80.0f);
		const float Shape = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shape"), 0.5)), 0.0f, 1.0f);
		const float Sharpness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ShapeSharpness"), 0.5)), 0.0f, 1.0f);
		const float DetailScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ShapeDetailScale"), 1.0)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

		FGaeaScalarField OrographicRain = MakeScalar(Height->Domain, TEXT("Erosion2Rain"));
		const bool bOrographic = Node.GetBool(TEXT("EnableOrographic"), false);
		const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)));
		const float Directional = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("DirectionalPrecipitation"), 0.5)), 0.0f, 1.0f);
		const float RainShadow = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RainShadow"), 0.5)), 0.0f, 1.0f);
		const float SlopeMin = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SlopeMin"), 0.0)), 0.0f, 90.0f);
		const float SlopeMax = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SlopeMax"), 90.0)), SlopeMin, 90.0f);
		const float AltMin = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("AltitudeMin"), 0.0)), 0.0f, 1.0f);
		const float AltMax = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("AltitudeMax"), 1.0)), AltMin, 1.0f);
		const bool bReverse = Node.GetBool(TEXT("Reverse"), false);
		const FVector2D Wind(FMath::Cos(Direction), FMath::Sin(Direction));
		const double ElevationMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		const FVector2d Spacing = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Height->Domain.Dimensions, Height->Domain.GetCellSize());
		for (int32 Y = 0; Y < Height->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
			{
				float Mask = 1.0f;
				if (bOrographic)
				{
					const int32 XL = FMath::Max(0, X - 1), XR = FMath::Min(Height->Domain.Dimensions.X - 1, X + 1);
					const int32 YD = FMath::Max(0, Y - 1), YU = FMath::Min(Height->Domain.Dimensions.Y - 1, Y + 1);
					const float GX = static_cast<float>((Height->AtInterior(XR, Y) - Height->AtInterior(XL, Y)) * ElevationMeters / FMath::Max((XR - XL) * Spacing.X, UE_DOUBLE_SMALL_NUMBER));
					const float GY = static_cast<float>((Height->AtInterior(X, YU) - Height->AtInterior(X, YD)) * ElevationMeters / FMath::Max((YU - YD) * Spacing.Y, UE_DOUBLE_SMALL_NUMBER));
					const float Slope = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(GX * GX + GY * GY)));
					const float Facing = FMath::Clamp(0.5f + 0.5f * (GX * Wind.X + GY * Wind.Y) / FMath::Max(FMath::Sqrt(GX * GX + GY * GY), UE_SMALL_NUMBER), 0.0f, 1.0f);
					const float Elev = FMath::Clamp(Height->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
					Mask = (Slope >= SlopeMin && Slope <= SlopeMax && Elev >= AltMin && Elev <= AltMax)
						? FMath::Lerp(1.0f - RainShadow, 1.0f, FMath::Lerp(0.5f, Facing, Directional)) : 0.0f;
					if (bReverse) Mask = 1.0f - Mask;
				}
				OrographicRain.AtInterior(X, Y) = FMath::Clamp(Mask, 0.0f, 1.0f);
			}
		}

		const float SuspendedDischarge = Suspended * FMath::Lerp(1.25f, 0.45f, SuspendedAngle / 80.0f);
		const float BedDischarge = Bed * FMath::Lerp(1.0f, 0.52f, BedAngle / 80.0f);
		const float CoarseDischarge = Coarse * FMath::Lerp(0.72f, 0.30f, CoarseAngle / 80.0f);
		const float AngleWeightedLoad = SuspendedDischarge + BedDischarge + CoarseDischarge;
		FGaeaHydraulicErosionSettings S;
		S.Iterations = Duration;
		S.Downcutting = Downcutting;
		S.FeatureScale = PhysicalScaleToSolver(ErosionScale, 2500.0);
		S.Strength = FMath::Clamp(0.42f + Shape * 0.95f, 0.0f, 1.5f);
		S.ErosionRate = FMath::Clamp(0.10f + 0.20f * Sharpness + 0.08f * Downcutting, 0.02f, 0.65f);
		S.Debris = FMath::Clamp(0.18f + Bed * 0.34f + Coarse * 0.48f, 0.0f, 1.0f);
		S.Volume = FMath::Clamp(0.45f + Suspended + Bed * 0.55f, 0.1f, 4.0f);
		S.SedimentCapacity = FMath::Clamp(0.28f + AngleWeightedLoad * 0.55f, 0.08f, 2.0f);
		// Erosion2's documented sediment controls determine discharge and resting
		// sediment. Do not inject an undocumented global deposition booster: it can
		// turn drainage paths into raised ribbons and makes the public controls lie.
		S.DepositionRate = FMath::Clamp(
			0.025f + BedDischarge * 0.055f + CoarseDischarge * 0.11f + SuspendedDischarge * 0.012f,
			0.01f,
			0.28f);
		S.MinimumSlope = FMath::Lerp(0.018f, 0.004f, DetailScale);
		S.Seed = Seed;
		S.bDeterministic = true;
		ConfigurePhysical(*Height, Input->HeightScale, Context, S);

		FGaeaHydraulicErosionResult R;
		if (!FGaeaHydraulicErosion::Evaluate(
			*Height,
			EffectiveSolverHeightScale(*Height, Input->HeightScale, Context),
			S,
			R,
			bOrographic ? &OrographicRain : Dataset.FindScalarField(GaeaTerrainFieldNames::Rainfall),
			Dataset.FindScalarField(GaeaTerrainFieldNames::HydraulicErosion),
			Dataset.FindScalarField(GaeaTerrainFieldNames::Deposition),
			Dataset.FindScalarField(GaeaTerrainFieldNames::Evaporation),
			Dataset.FindScalarField(GaeaTerrainFieldNames::RockHardness),
			Dataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth)))
		{
			Error = TEXT("Erosion2 solver failed."); return false;
		}
		return PublishResult(MoveTemp(Dataset), Input->HeightScale, MoveTemp(R), Out, Error, Context);
	}

	void RegisterErosion()
	{
		FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::HydraulicErosion; D.DisplayName = TEXT("Erosion"); D.Category = TEXT("Simulate");
		D.Description = TEXT("Resolution-independent hydraulic erosion with physical feature scale, sediment transport, downcutting and selective processing.");
		D.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain"))); D.Inputs.Add(Port(TEXT("Area"), TEXT("Area"), TEXT("ScalarField")));
		D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Wear"), TEXT("Wear"), TEXT("ScalarField"))); D.Outputs.Add(Port(TEXT("Deposits"), TEXT("Deposits"), TEXT("ScalarField"))); D.Outputs.Add(Port(TEXT("Flow"), TEXT("Flow"), TEXT("ScalarField")));
		D.Parameters = {
			Int(TEXT("Duration"), TEXT("Duration"), 24, 1, 4096, TEXT("Erosion")), Num(TEXT("RockSoftness"), TEXT("Rock Softness"), 0.0, 0.0, 1.0, TEXT("Erosion")), Num(TEXT("Strength"), TEXT("Strength"), 1.0, 0.0, 4.0, TEXT("Erosion")),
			Num(TEXT("Downcutting"), TEXT("Downcutting"), 0.5, 0.0, 2.0, TEXT("Downcutting")), Num(TEXT("Inhibition"), TEXT("Inhibition"), 0.0, 0.0, 1.0, TEXT("Downcutting")), Num(TEXT("BaseLevel"), TEXT("Base Level"), -1.0, -1.0, 1.0, TEXT("Downcutting")),
			Num(TEXT("FeatureScale"), TEXT("Feature Scale (m)"), 2000.0, 1.0, 20000.0, TEXT("Scale")), Bool(TEXT("RealScale"), TEXT("Real Scale"), true, TEXT("Scale")), Num(TEXT("TerrainScale"), TEXT("Terrain Scale"), 1.0, 0.01, 100.0, TEXT("Scale")), Num(TEXT("Verticality"), TEXT("Verticality"), 1.0, 0.01, 10.0, TEXT("Scale")),
			Num(TEXT("Debris"), TEXT("Debris"), 0.5, 0.0, 1.0, TEXT("Flow")), Num(TEXT("Volume"), TEXT("Volume"), 1.0, 0.0, 4.0, TEXT("Flow")), Num(TEXT("SedimentRemoval"), TEXT("Sediment Removal"), 0.0, 0.0, 1.0, TEXT("Flow")),
			Choice(TEXT("AreaEffect"), TEXT("Area Effect"), TEXT("None"), { TEXT("ErosionStrength"), TEXT("RockSoftness"), TEXT("PrecipitationAmount"), TEXT("None") }, TEXT("Selective Processing")), Choice(TEXT("BiasType"), TEXT("Bias Type"), TEXT("Altitude"), { TEXT("Altitude"), TEXT("Slope") }, TEXT("Selective Processing")), Num(TEXT("Bias"), TEXT("Bias"), 0.5, 0.0, 1.0, TEXT("Selective Processing")), Bool(TEXT("Reverse"), TEXT("Reverse"), false, TEXT("Selective Processing")),
			Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647), Bool(TEXT("AggressiveMode"), TEXT("Aggressive Mode"), false), Bool(TEXT("Deterministic"), TEXT("Deterministic"), true)
		};
		FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateErosion);
	}

	void RegisterErosion2()
	{
		FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::Erosion2; D.DisplayName = TEXT("Erosion2"); D.Category = TEXT("Simulate");
		D.Description = TEXT("Advanced hydraulic erosion with physical erosion scale, documented sediment discharge controls, shape controls and orographic rainfall.");
		D.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain")));
		D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Wear"), TEXT("Wear"), TEXT("ScalarField"))); D.Outputs.Add(Port(TEXT("Deposits"), TEXT("Deposits"), TEXT("ScalarField"))); D.Outputs.Add(Port(TEXT("Flow"), TEXT("Flow"), TEXT("ScalarField")));
		D.Parameters = {
			Int(TEXT("Duration"), TEXT("Duration"), 64, 1, 512, TEXT("General")), Num(TEXT("Downcutting"), TEXT("Downcutting"), 0.35, 0.0, 1.0, TEXT("General")), Num(TEXT("ErosionScale"), TEXT("Erosion Scale"), 500.0, 1.0, 20000.0, TEXT("General")), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("General")),
			Num(TEXT("SuspendedLoad"), TEXT("Suspended Load"), 0.5, 0.0, 1.0, TEXT("Sediment Discharge")), Num(TEXT("SuspendedDischargeAngle"), TEXT("Discharge Angle"), 18.0, 0.0, 80.0, TEXT("Sediment Discharge")), Num(TEXT("BedLoad"), TEXT("Bed Load"), 0.45, 0.0, 1.0, TEXT("Sediment Discharge")), Num(TEXT("BedDischargeAngle"), TEXT("Discharge Angle"), 28.0, 0.0, 80.0, TEXT("Sediment Discharge")), Num(TEXT("CoarseSediments"), TEXT("Coarse Sediments"), 0.3, 0.0, 1.0, TEXT("Sediment Discharge")), Num(TEXT("CoarseDischargeAngle"), TEXT("Discharge Angle"), 38.0, 0.0, 80.0, TEXT("Sediment Discharge")),
			Num(TEXT("Shape"), TEXT("Shape"), 0.5, 0.0, 1.0, TEXT("Shape")), Num(TEXT("ShapeSharpness"), TEXT("Shape Sharpness"), 0.5, 0.0, 1.0, TEXT("Shape")), Num(TEXT("ShapeDetailScale"), TEXT("Shape Detail Scale"), 1.0, 0.0, 1.0, TEXT("Shape")),
			Bool(TEXT("EnableOrographic"), TEXT("Enable"), false, TEXT("Orographic Influence")), Num(TEXT("DirectionalPrecipitation"), TEXT("Directional Precipitation"), 0.5, 0.0, 1.0, TEXT("Orographic Influence")), Num(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0, TEXT("Orographic Influence")), Num(TEXT("RainShadow"), TEXT("Rain Shadow"), 0.5, 0.0, 1.0, TEXT("Orographic Influence")), Num(TEXT("SlopeMin"), TEXT("Slope Min"), 0.0, 0.0, 90.0, TEXT("Orographic Influence")), Num(TEXT("SlopeMax"), TEXT("Slope Max"), 90.0, 0.0, 90.0, TEXT("Orographic Influence")), Num(TEXT("AltitudeMin"), TEXT("Altitude Min"), 0.0, 0.0, 1.0, TEXT("Orographic Influence")), Num(TEXT("AltitudeMax"), TEXT("Altitude Max"), 1.0, 0.0, 1.0, TEXT("Orographic Influence")), Bool(TEXT("Reverse"), TEXT("Reverse"), false, TEXT("Orographic Influence"))
		};
		FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateErosion2);
	}
}

void RegisterGaeaReferenceFidelityProcessNodes()
{
	using namespace GaeaReferenceFidelityProcess;
	RegisterErosion();
	RegisterErosion2();
}
