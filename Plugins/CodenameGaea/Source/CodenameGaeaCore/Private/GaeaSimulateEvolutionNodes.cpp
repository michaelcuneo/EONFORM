#include "GaeaSimulateEvolutionNodes.h"

#include "GaeaHydraulicErosion.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaThermalErosion.h"

namespace GaeaSimulateEvolution
{
	FGaeaTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* Label)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = TEXT("Terrain");
		return P;
	}

	FGaeaTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* Label)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = TEXT("ScalarField");
		return P;
	}

	FGaeaTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group = nullptr)
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

	FGaeaTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = static_cast<double>(Max);
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName Option : Options) P.NameOptions.Add(Option);
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Range(FName Name, const TCHAR* Label, double DefaultMin, double DefaultMax, double Min, double Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Range;
		P.DefaultRangeMin = DefaultMin;
		P.DefaultRangeMax = DefaultMax;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		if (Group) P.Group = Group;
		return P;
	}

	const FGaeaTerrainValue* RequireTerrain(const FGaeaTerrainNodeInputs& Inputs, const TCHAR* NodeName, FString& Error)
	{
		const FGaeaTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = FString::Printf(TEXT("%s requires a valid terrain input 'Terrain'."), NodeName);
			return nullptr;
		}
		return Input;
	}

	bool PublishTerrain(FGaeaTerrainDataset&& Dataset, float HeightScale, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		FGaeaTerrainValue Value = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Value.IsValid())
		{
			Error = TEXT("Simulate node produced an invalid terrain output.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Value));
		return true;
	}

	FGaeaScalarField MakeScalar(const FGaeaGridDomain& Domain, FName Name, EGaeaFieldUnit Unit = EGaeaFieldUnit::Normalized)
	{
		FGaeaFieldDescriptor D;
		D.Name = Name;
		D.Unit = Unit;
		D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, D);
		return Field;
	}

	float Hash01(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u;
		H ^= static_cast<uint32>(Y) * 668265263u;
		H ^= static_cast<uint32>(Seed) * 2246822519u;
		H = (H ^ (H >> 13u)) * 1274126177u;
		H ^= H >> 16u;
		return static_cast<float>(H & 0x00ffffffu) / 16777215.0f;
	}

	float Smooth01(float V)
	{
		V = FMath::Clamp(V, 0.0f, 1.0f);
		return V * V * (3.0f - 2.0f * V);
	}

	float EffectiveSolverHeightScale(const FGaeaScalarField& Height, float LegacyHeightScale, const FGaeaTerrainEvaluationContext& Context)
	{
		const FVector2d DomainCell = Height.Domain.GetCellSize();
		const double DomainRepresentative = FMath::Max(FMath::Min(FMath::Abs(DomainCell.X), FMath::Abs(DomainCell.Y)), UE_DOUBLE_SMALL_NUMBER);
		const double PhysicalSpacing = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(Height.Domain.Dimensions, DomainCell);
		const double PhysicalElevation = Context.PhysicalMetrics.ResolveElevationScaleMeters(LegacyHeightScale);
		return static_cast<float>(FMath::Max(PhysicalElevation / FMath::Max(PhysicalSpacing, UE_DOUBLE_SMALL_NUMBER) * DomainRepresentative, 1.0));
	}

	void ConfigurePhysicalHydraulic(const FGaeaScalarField& Height, float HeightScale, const FGaeaTerrainEvaluationContext& Context, FGaeaHydraulicErosionSettings& Settings)
	{
		Settings.PhysicalSampleSpacingMeters = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(Height.Domain.Dimensions, Height.Domain.GetCellSize());
		Settings.PhysicalElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(HeightScale);
	}

	bool RunHydraulic(
		const FGaeaTerrainValue& Input,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaHydraulicErosionSettings Settings,
		const FGaeaScalarField* RainfallMask,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		FGaeaTerrainDataset Dataset = Input.TerrainDataset;
		FGaeaTerrainDerivedDataSettings Derived;
		if (!FGaeaTerrainDerivedData::EnsureHydraulicInputs(Dataset, Input.HeightScale, Context.PhysicalMetrics, Derived, &Error)) return false;
		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Hydraulic simulation input has no valid Height field.");
			return false;
		}

		ConfigurePhysicalHydraulic(*Height, Input.HeightScale, Context, Settings);
		FGaeaHydraulicErosionResult Result;
		if (!FGaeaHydraulicErosion::Evaluate(
			*Height,
			EffectiveSolverHeightScale(*Height, Input.HeightScale, Context),
			Settings,
			Result,
			RainfallMask ? RainfallMask : Dataset.FindScalarField(GaeaTerrainFieldNames::Rainfall),
			Dataset.FindScalarField(GaeaTerrainFieldNames::HydraulicErosion),
			Dataset.FindScalarField(GaeaTerrainFieldNames::Deposition),
			Dataset.FindScalarField(GaeaTerrainFieldNames::Evaporation),
			Dataset.FindScalarField(GaeaTerrainFieldNames::RockHardness),
			Dataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth)))
		{
			Error = TEXT("Hydraulic simulation failed.");
			return false;
		}

		FGaeaScalarField WearOutput = Result.Wear;
		FGaeaScalarField DepositsOutput = Result.Deposits;
		FGaeaScalarField FlowOutput = Result.Flow;
		Dataset.SetScalarField(MoveTemp(Result.Height));
		Dataset.SetScalarField(MoveTemp(Result.Wear));
		Dataset.SetScalarField(MoveTemp(Result.Deposits));
		Dataset.SetScalarField(MoveTemp(Result.Flow));
		if (!PublishTerrain(MoveTemp(Dataset), Input.HeightScale, Out, Error)) return false;
		Out.Outputs.Add(TEXT("Wear"), FGaeaTerrainValue::MakeScalarField(MoveTemp(WearOutput)));
		Out.Outputs.Add(TEXT("Deposits"), FGaeaTerrainValue::MakeScalarField(MoveTemp(DepositsOutput)));
		Out.Outputs.Add(TEXT("Flow"), FGaeaTerrainValue::MakeScalarField(MoveTemp(FlowOutput)));
		return true;
	}

	struct FEasyProfile
	{
		float Strength = 0.7f;
		float Downcutting = 0.45f;
		float FeatureScale = 1.0f;
		float Debris = 0.45f;
		float Volume = 1.0f;
		float SedimentRemoval = 0.05f;
	};

	FEasyProfile ResolveEasyProfile(FName Style)
	{
		FEasyProfile P;
		if (Style == TEXT("Ancient")) { P.Strength = 0.55f; P.Downcutting = 0.25f; P.FeatureScale = 1.8f; P.Debris = 0.7f; }
		else if (Style == TEXT("Ancient 2")) { P.Strength = 0.7f; P.Downcutting = 0.3f; P.FeatureScale = 2.2f; P.Debris = 0.8f; }
		else if (Style == TEXT("Alpine")) { P.Strength = 0.9f; P.Downcutting = 0.8f; P.FeatureScale = 0.8f; P.Debris = 0.35f; P.SedimentRemoval = 0.2f; }
		else if (Style == TEXT("Rocky")) { P.Strength = 0.8f; P.Downcutting = 0.6f; P.FeatureScale = 0.65f; P.Debris = 0.25f; P.SedimentRemoval = 0.3f; }
		else if (Style == TEXT("Exposed")) { P.Strength = 0.65f; P.Downcutting = 0.55f; P.FeatureScale = 0.9f; P.Debris = 0.1f; P.SedimentRemoval = 0.55f; }
		else if (Style == TEXT("Flows")) { P.Strength = 0.75f; P.Downcutting = 0.7f; P.FeatureScale = 1.1f; P.Volume = 1.4f; }
		else if (Style == TEXT("Flows 2")) { P.Strength = 0.9f; P.Downcutting = 0.8f; P.FeatureScale = 1.4f; P.Volume = 1.6f; }
		else if (Style == TEXT("Flows 3")) { P.Strength = 1.0f; P.Downcutting = 0.95f; P.FeatureScale = 1.8f; P.Volume = 1.8f; }
		else if (Style == TEXT("Strata")) { P.Strength = 0.55f; P.Downcutting = 0.45f; P.FeatureScale = 0.45f; P.Debris = 0.4f; }
		else if (Style == TEXT("Withered")) { P.Strength = 0.8f; P.Downcutting = 0.4f; P.FeatureScale = 1.5f; P.Debris = 0.8f; P.SedimentRemoval = 0.1f; }
		else if (Style == TEXT("Soft Soil")) { P.Strength = 0.45f; P.Downcutting = 0.3f; P.FeatureScale = 1.1f; P.Debris = 0.9f; P.Volume = 1.4f; }
		else if (Style == TEXT("Soft Soil 2")) { P.Strength = 0.6f; P.Downcutting = 0.35f; P.FeatureScale = 1.5f; P.Debris = 1.0f; P.Volume = 1.6f; }
		else if (Style == TEXT("Dessicated")) { P.Strength = 0.85f; P.Downcutting = 0.65f; P.FeatureScale = 0.35f; P.Debris = 0.15f; P.SedimentRemoval = 0.45f; }
		else if (Style == TEXT("Thin")) { P.Strength = 0.4f; P.Downcutting = 0.65f; P.FeatureScale = 0.5f; P.Debris = 0.1f; P.Volume = 0.6f; }
		return P;
	}

	bool EvaluateEasyErosion(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrain(Inputs, TEXT("EasyErosion"), Error);
		if (!Input) return false;
		const FName Style = Node.GetName(TEXT("Style"), TEXT("Simple"));
		const float Influence = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Influence"), 0.75)), 0.0f, 1.0f);
		const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)));
		const FName Bias = Node.GetName(TEXT("BiasAngle"), TEXT("X"));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const FEasyProfile Profile = ResolveEasyProfile(Style);

		FGaeaTerrainDataset Prepared = Input->TerrainDataset;
		const FGaeaScalarField* Height = Prepared.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid()) { Error = TEXT("EasyErosion input has no valid Height field."); return false; }
		FGaeaScalarField Rain = MakeScalar(Height->Domain, TEXT("EasyErosionRain"));
		FVector2D Dir(FMath::Cos(Direction), FMath::Sin(Direction));
		if (Bias == TEXT("N")) Dir = FVector2D(0.0f, 1.0f);
		else if (Bias == TEXT("E")) Dir = FVector2D(1.0f, 0.0f);
		else if (Bias == TEXT("S")) Dir = FVector2D(0.0f, -1.0f);
		else if (Bias == TEXT("W")) Dir = FVector2D(-1.0f, 0.0f);
		for (int32 Y = 0; Y < Height->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
			{
				const FVector2d W = Height->Domain.InteriorSampleToWorld(X, Y);
				const FVector2d Size = Height->Domain.WorldSize();
				const float NX = Size.X > UE_DOUBLE_SMALL_NUMBER ? static_cast<float>((W.X - Height->Domain.WorldMin.X) / Size.X - 0.5) : 0.0f;
				const float NY = Size.Y > UE_DOUBLE_SMALL_NUMBER ? static_cast<float>((W.Y - Height->Domain.WorldMin.Y) / Size.Y - 0.5) : 0.0f;
				const float Directional = Bias == TEXT("X") ? 0.5f : FMath::Clamp(0.5f + 0.75f * (NX * Dir.X + NY * Dir.Y), 0.0f, 1.0f);
				const float Jitter = FMath::Lerp(0.8f, 1.2f, Hash01(X, Y, Seed));
				Rain.AtInterior(X, Y) = FMath::Clamp(FMath::Lerp(1.0f, Directional, Influence * 0.65f) * Jitter, 0.0f, 1.0f);
			}
		}

		FGaeaHydraulicErosionSettings Settings;
		Settings.Iterations = FMath::Clamp(FMath::RoundToInt(FMath::Lerp(6.0f, 48.0f, Influence)), 1, 128);
		Settings.Strength = Profile.Strength * Influence;
		Settings.Downcutting = Profile.Downcutting;
		Settings.FeatureScale = Profile.FeatureScale;
		Settings.Debris = Profile.Debris;
		Settings.Volume = Profile.Volume;
		Settings.SedimentRemoval = Profile.SedimentRemoval;
		Settings.Seed = Seed;
		return RunHydraulic(*Input, Context, Settings, &Rain, Out, Error);
	}

	bool EvaluateErosion2(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrain(Inputs, TEXT("Erosion2"), Error);
		if (!Input) return false;
		FGaeaTerrainDataset Prepared = Input->TerrainDataset;
		const FGaeaScalarField* Height = Prepared.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid()) { Error = TEXT("Erosion2 input has no valid Height field."); return false; }

		const int32 Duration = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Duration"), 32)), 1, 512);
		const float Downcutting = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Downcutting"), 0.55)), 0.0f, 1.0f);
		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ErosionScale"), 1.0)), 0.1f, 8.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Suspended = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SuspendedLoad"), 0.5)), 0.0f, 1.0f);
		const float SuspendedAngle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SuspendedDischargeAngle"), 18.0)), 0.0f, 80.0f);
		const float Bed = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("BedLoad"), 0.45)), 0.0f, 1.0f);
		const float BedAngle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("BedDischargeAngle"), 28.0)), 0.0f, 80.0f);
		const float Coarse = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("CoarseSediments"), 0.3)), 0.0f, 1.0f);
		const float CoarseAngle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("CoarseDischargeAngle"), 38.0)), 0.0f, 80.0f);
		const float DepositionBoost = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("DepositionBoost"), 0.2)), 0.0f, 2.0f);
		const float ExtraDeposition = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ExtraDepositionBoost"), 0.0)), 0.0f, 2.0f);
		const float Shape = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shape"), 0.5)), 0.0f, 1.0f);
		const float Sharpness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ShapeSharpness"), 0.5)), 0.0f, 1.0f);
		const float DetailScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ShapeDetailScale"), 1.0)), 0.1f, 8.0f);
		const bool bOrographic = Node.GetBool(TEXT("EnableOrographic"), false);
		const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)));
		const float DirectionalPrecip = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("DirectionalPrecipitation"), 0.5)), 0.0f, 1.0f);
		const float RainShadow = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RainShadow"), 0.5)), 0.0f, 1.0f);
		const float SlopeMin = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SlopeMin"), 0.0)), 0.0f, 90.0f);
		const float SlopeMax = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SlopeMax"), 90.0)), SlopeMin, 90.0f);
		const float AltMin = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("AltitudeMin"), 0.0)), 0.0f, 1.0f);
		const float AltMax = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("AltitudeMax"), 1.0)), AltMin, 1.0f);
		const bool bReverse = Node.GetBool(TEXT("Reverse"), false);

		FGaeaScalarField Rain = MakeScalar(Height->Domain, TEXT("Erosion2Rain"));
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
					const float SlopeDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(GX * GX + GY * GY)));
					const float Facing = FMath::Clamp(0.5f + 0.5f * (GX * Wind.X + GY * Wind.Y) / FMath::Max(FMath::Sqrt(GX * GX + GY * GY), UE_SMALL_NUMBER), 0.0f, 1.0f);
					const float Elev01 = FMath::Clamp(Height->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
					const float SlopeMask = (SlopeDeg >= SlopeMin && SlopeDeg <= SlopeMax) ? 1.0f : 0.0f;
					const float AltMask = (Elev01 >= AltMin && Elev01 <= AltMax) ? 1.0f : 0.0f;
					Mask = FMath::Lerp(1.0f - RainShadow, 1.0f, FMath::Lerp(0.5f, Facing, DirectionalPrecip)) * SlopeMask * AltMask;
					if (bReverse) Mask = 1.0f - Mask;
				}
				Rain.AtInterior(X, Y) = FMath::Clamp(Mask, 0.0f, 1.0f);
			}
		}

		const float AngleWeightedLoad = Suspended * FMath::Lerp(1.2f, 0.45f, SuspendedAngle / 80.0f)
			+ Bed * FMath::Lerp(1.0f, 0.55f, BedAngle / 80.0f)
			+ Coarse * FMath::Lerp(0.75f, 0.35f, CoarseAngle / 80.0f);
		FGaeaHydraulicErosionSettings Settings;
		Settings.Iterations = Duration;
		Settings.Strength = FMath::Clamp(0.55f + Shape * 0.8f, 0.0f, 1.5f);
		Settings.Downcutting = Downcutting;
		Settings.FeatureScale = Scale * DetailScale;
		Settings.Debris = FMath::Clamp(0.25f + Bed * 0.35f + Coarse * 0.4f, 0.0f, 1.0f);
		Settings.Volume = FMath::Clamp(0.6f + Suspended + Bed * 0.5f, 0.1f, 4.0f);
		Settings.SedimentCapacity = FMath::Clamp(0.35f + AngleWeightedLoad * 0.45f, 0.1f, 2.0f);
		Settings.DepositionRate = FMath::Clamp(0.08f + 0.12f * (DepositionBoost + ExtraDeposition) + 0.08f * Coarse, 0.01f, 0.6f);
		Settings.ErosionRate = FMath::Clamp(0.12f + 0.16f * Sharpness + 0.1f * Downcutting, 0.02f, 0.6f);
		Settings.Seed = Seed;
		return RunHydraulic(*Input, Context, Settings, bOrographic ? &Rain : nullptr, Out, Error);
	}

	bool EvaluateThermal2(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrain(Inputs, TEXT("Thermal2"), Error);
		if (!Input) return false;
		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		FGaeaTerrainDerivedDataSettings Derived;
		if (!FGaeaTerrainDerivedData::EnsureHydraulicInputs(Dataset, Input->HeightScale, Context.PhysicalMetrics, Derived, &Error)) return false;
		const FGaeaScalarField* Source = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Source || !Source->IsValid()) { Error = TEXT("Thermal2 input has no valid Height field."); return false; }

		const int32 Duration = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Duration"), 16)), 1, 1024);
		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.45)), 0.0f, 1.0f);
		const float Anisotropy = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Anisotropy"), 0.0)), 0.0f, 1.0f);
		const float Angle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Angle"), 34.0)), 0.0f, 89.9f);
		const float SedimentRemoval = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SedimentRemoval"), 0.0)), 0.0f, 1.0f);
		const float FeatureScaleMeters = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("FeatureScale"), 20.0)), 0.1f, 10000.0f);
		const double SampleSpacing = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(Source->Domain.Dimensions, Source->Domain.GetCellSize());
		const float ScaleResponse = FMath::Clamp(static_cast<float>(FeatureScaleMeters / FMath::Max(SampleSpacing, UE_DOUBLE_SMALL_NUMBER)), 0.15f, 4.0f);

		FGaeaThermalErosionSettings Settings;
		Settings.Iterations = Duration;
		Settings.TalusAngleDegrees = FMath::Clamp(Angle * FMath::Lerp(1.0f, 0.82f, Anisotropy), 0.0f, 89.9f);
		Settings.Strength = FMath::Clamp(Strength * ScaleResponse * (1.0f - 0.65f * SedimentRemoval), 0.0f, 1.0f);
		FGaeaScalarField Height = *Source;
		if (!FGaeaThermalErosion::ApplyInPlace(
			Height,
			EffectiveSolverHeightScale(*Source, Input->HeightScale, Context),
			Settings,
			Dataset.FindScalarField(GaeaTerrainFieldNames::Thermal),
			Dataset.FindScalarField(GaeaTerrainFieldNames::RockHardness),
			nullptr,
			&Error)) return false;

		FGaeaScalarField Talus = MakeScalar(Source->Domain, TEXT("Talus"));
		for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X)
			{
				Talus.AtInterior(X, Y) = FMath::Clamp((Source->AtInterior(X, Y) - Height.AtInterior(X, Y)) * 8.0f + 0.5f, 0.0f, 1.0f);
			}
		}
		FGaeaScalarField TalusOutput = Talus;
		Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
		Dataset.SetScalarField(MoveTemp(Height));
		Dataset.SetScalarField(Talus);
		if (!PublishTerrain(MoveTemp(Dataset), Input->HeightScale, Out, Error)) return false;
		Out.Outputs.Add(TEXT("Talus"), FGaeaTerrainValue::MakeScalarField(MoveTemp(TalusOutput)));
		return true;
	}

	bool EvaluateCrumble(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrain(Inputs, TEXT("Crumble"), Error);
		if (!Input) return false;
		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		FGaeaTerrainDerivedDataSettings Derived;
		if (!FGaeaTerrainDerivedData::EnsureHydraulicInputs(Dataset, Input->HeightScale, Context.PhysicalMetrics, Derived, &Error)) return false;
		if (!FGaeaTerrainDerivedData::EnsureFlowAnalysis(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;
		const FGaeaScalarField* Source = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* Flow = Dataset.FindScalarField(GaeaTerrainFieldNames::FlowAccumulation);
		const FGaeaScalarField* Deposits = Dataset.FindScalarField(GaeaTerrainFieldNames::Deposition);
		if (!Source || !Flow) { Error = TEXT("Crumble could not resolve terrain flow-analysis fields."); return false; }

		const int32 Duration = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Duration"), 8)), 1, 128);
		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.45)), 0.0f, 1.0f);
		const float Coverage = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Coverage"), 0.65)), 0.0f, 1.0f);
		const float Horizontal = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Horizontal"), 0.0)), -1.0f, 1.0f);
		const float Vertical = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Vertical"), 0.0)), -1.0f, 1.0f);
		const float RockHardness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RockHardness"), 0.35)), 0.0f, 1.0f);
		const float Edge = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Edge"), 0.5)), 0.0f, 1.0f);
		const float Downcutting = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Downcutting"), 0.35)), 0.0f, 1.0f);
		const float Depth = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Depth"), 0.4)), 0.0f, 1.0f);
		const FName Direction = Node.GetName(TEXT("Direction"), TEXT("X"));
		FVector2D Dir(0.0f, 0.0f);
		if (Direction == TEXT("N")) Dir = FVector2D(0.0f, 1.0f);
		else if (Direction == TEXT("E")) Dir = FVector2D(1.0f, 0.0f);
		else if (Direction == TEXT("S")) Dir = FVector2D(0.0f, -1.0f);
		else if (Direction == TEXT("W")) Dir = FVector2D(-1.0f, 0.0f);

		float MaxFlow = 0.0f;
		for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X) MaxFlow = FMath::Max(MaxFlow, Flow->AtInterior(X, Y));
		const double ElevationMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		FGaeaScalarField Height = *Source;
		FGaeaScalarField Crumble = MakeScalar(Source->Domain, TEXT("Crumble"));
		for (int32 Pass = 0; Pass < Duration; ++Pass)
		{
			const FGaeaScalarField Previous = Height;
			for (int32 Y = 1; Y < Source->Domain.Dimensions.Y - 1; ++Y)
			{
				for (int32 X = 1; X < Source->Domain.Dimensions.X - 1; ++X)
				{
					const float Center = Previous.AtInterior(X, Y);
					const float GX = 0.5f * (Previous.AtInterior(X + 1, Y) - Previous.AtInterior(X - 1, Y));
					const float GY = 0.5f * (Previous.AtInterior(X, Y + 1) - Previous.AtInterior(X, Y - 1));
					const float Lap = FMath::Abs(Previous.AtInterior(X - 1, Y) + Previous.AtInterior(X + 1, Y) + Previous.AtInterior(X, Y - 1) + Previous.AtInterior(X, Y + 1) - 4.0f * Center);
					const float EdgeSignal = FMath::Clamp((FMath::Sqrt(GX * GX + GY * GY) * 8.0f + Lap * 6.0f) * FMath::Lerp(0.5f, 1.8f, Edge), 0.0f, 1.0f);
					const float Flow01 = MaxFlow > UE_SMALL_NUMBER ? FMath::Clamp(FMath::Loge(1.0f + Flow->AtInterior(X, Y)) / FMath::Loge(1.0f + MaxFlow), 0.0f, 1.0f) : 0.0f;
					const float Deposit01 = Deposits ? FMath::Clamp(Deposits->AtInterior(X, Y), 0.0f, 1.0f) : 0.0f;
					const float HorizontalBias = FMath::Clamp(0.5f + Horizontal * (Deposit01 - Flow01), 0.0f, 1.0f);
					const float Elev01 = FMath::Clamp(Center * 0.5f + 0.5f, 0.0f, 1.0f);
					const float VerticalBias = FMath::Clamp(0.5f + Vertical * (Elev01 - 0.5f), 0.0f, 1.0f);
					const float DirectionBias = Direction == TEXT("X") ? 1.0f : FMath::Clamp(0.5f + 3.0f * (GX * Dir.X + GY * Dir.Y), 0.0f, 1.0f);
					const float Mask = EdgeSignal * FMath::Lerp(0.35f, 1.0f, Coverage) * FMath::Lerp(0.6f, 1.4f, HorizontalBias) * FMath::Lerp(0.6f, 1.4f, VerticalBias) * DirectionBias * (1.0f - RockHardness);
					const float CutMeters = FMath::Lerp(0.15f, 8.0f, Depth) * Strength * Mask / static_cast<float>(Duration) + Downcutting * Flow01 * 1.5f * Mask / static_cast<float>(Duration);
					Height.AtInterior(X, Y) = FMath::Clamp(Center - static_cast<float>(CutMeters / FMath::Max(ElevationMeters, 1.0)), -1.0f, 1.0f);
					Crumble.AtInterior(X, Y) = FMath::Max(Crumble.AtInterior(X, Y), FMath::Clamp(Mask, 0.0f, 1.0f));
				}
			}
		}
		FGaeaScalarField CrumbleOutput = Crumble;
		Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
		Dataset.SetScalarField(MoveTemp(Height));
		Dataset.SetScalarField(Crumble);
		if (!PublishTerrain(MoveTemp(Dataset), Input->HeightScale, Out, Error)) return false;
		Out.Outputs.Add(TEXT("Crumble"), FGaeaTerrainValue::MakeScalarField(MoveTemp(CrumbleOutput)));
		return true;
	}

	bool EvaluateHillify(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrain(Inputs, TEXT("Hillify"), Error);
		if (!Input) return false;
		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		const FGaeaScalarField* Source = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Source || !Source->IsValid()) { Error = TEXT("Hillify input has no valid Height field."); return false; }
		const float Coverage = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Coverage"), 0.75)), 0.0f, 1.0f);
		const FName Creep = Node.GetName(TEXT("Creep"), TEXT("Moderate"));
		const FName Surface = Node.GetName(TEXT("Surface"), TEXT("Smooth"));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const int32 Passes = Creep == TEXT("Aggressive") ? 12 : 6;
		const float Blend = Creep == TEXT("Aggressive") ? 0.48f : 0.3f;
		FGaeaScalarField Height = *Source;
		FGaeaScalarField HillMask = MakeScalar(Source->Domain, TEXT("Hillify"));
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			const FGaeaScalarField Previous = Height;
			for (int32 Y = 1; Y < Source->Domain.Dimensions.Y - 1; ++Y)
			{
				for (int32 X = 1; X < Source->Domain.Dimensions.X - 1; ++X)
				{
					const float Noise = Hash01(X >> 2, Y >> 2, Seed);
					const float Mask = Smooth01(FMath::Clamp((Coverage - (1.0f - Noise)) * 3.0f + 0.5f, 0.0f, 1.0f));
					const float Average = (Previous.AtInterior(X - 1, Y) + Previous.AtInterior(X + 1, Y) + Previous.AtInterior(X, Y - 1) + Previous.AtInterior(X, Y + 1) + Previous.AtInterior(X, Y)) / 5.0f;
					float Target = FMath::Lerp(Previous.AtInterior(X, Y), Average, Blend * Mask);
					if (Surface == TEXT("Eroded"))
					{
						const float Micro = FMath::PerlinNoise2D(FVector2D(X, Y) * 0.065f + FVector2D(Seed * 0.013f, Seed * 0.021f));
						Target -= FMath::Abs(Micro) * 0.0025f * Mask;
					}
					Height.AtInterior(X, Y) = FMath::Clamp(Target, -1.0f, 1.0f);
					HillMask.AtInterior(X, Y) = FMath::Max(HillMask.AtInterior(X, Y), Mask);
				}
			}
		}
		FGaeaScalarField HillOutput = HillMask;
		Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
		Dataset.SetScalarField(MoveTemp(Height));
		Dataset.SetScalarField(HillMask);
		if (!PublishTerrain(MoveTemp(Dataset), Input->HeightScale, Out, Error)) return false;
		Out.Outputs.Add(TEXT("Mask"), FGaeaTerrainValue::MakeScalarField(MoveTemp(HillOutput)));
		return true;
	}

	void RegisterEasyErosion()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::EasyErosion;
		D.DisplayName = TEXT("EasyErosion");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Applies curated erosion styles through EONFORM's physical hydraulic erosion engine.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Wear"), TEXT("Wear")));
		D.Outputs.Add(ScalarPort(TEXT("Deposits"), TEXT("Deposits")));
		D.Outputs.Add(ScalarPort(TEXT("Flow"), TEXT("Flow")));
		D.Parameters.Add(Choice(TEXT("Style"), TEXT("Style"), TEXT("Simple"), { TEXT("Simple"), TEXT("Ancient"), TEXT("Ancient 2"), TEXT("Alpine"), TEXT("Rocky"), TEXT("Exposed"), TEXT("Flows"), TEXT("Flows 2"), TEXT("Flows 3"), TEXT("Strata"), TEXT("Withered"), TEXT("Soft Soil"), TEXT("Soft Soil 2"), TEXT("Dessicated"), TEXT("Thin") }, TEXT("Erosion")));
		D.Parameters.Add(Num(TEXT("Influence"), TEXT("Influence"), 0.75, 0.0, 1.0, TEXT("Erosion")));
		D.Parameters.Add(Num(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0, TEXT("Erosion")));
		D.Parameters.Add(Choice(TEXT("BiasAngle"), TEXT("Bias Angle"), TEXT("X"), { TEXT("X"), TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") }, TEXT("Erosion")));
		D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Erosion")));
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateEasyErosion);
	}

	void RegisterErosion2()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::Erosion2;
		D.DisplayName = TEXT("Erosion2");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Advanced physical hydraulic erosion with sediment classes, deposition shaping, and optional directional precipitation.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Wear"), TEXT("Wear")));
		D.Outputs.Add(ScalarPort(TEXT("Deposits"), TEXT("Deposits")));
		D.Outputs.Add(ScalarPort(TEXT("Flow"), TEXT("Flow")));
		D.Parameters = {
			Int(TEXT("Duration"), TEXT("Duration"), 32, 1, 512, TEXT("General")),
			Num(TEXT("Downcutting"), TEXT("Downcutting"), 0.55, 0.0, 1.0, TEXT("General")),
			Num(TEXT("ErosionScale"), TEXT("Erosion Scale"), 1.0, 0.1, 8.0, TEXT("General")),
			Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("General")),
			Num(TEXT("SuspendedLoad"), TEXT("Suspended Load"), 0.5, 0.0, 1.0, TEXT("Sediment Discharge")),
			Num(TEXT("SuspendedDischargeAngle"), TEXT("Discharge Angle"), 18.0, 0.0, 80.0, TEXT("Sediment Discharge")),
			Num(TEXT("BedLoad"), TEXT("Bed Load"), 0.45, 0.0, 1.0, TEXT("Sediment Discharge")),
			Num(TEXT("BedDischargeAngle"), TEXT("Discharge Angle (Bed)"), 28.0, 0.0, 80.0, TEXT("Sediment Discharge")),
			Num(TEXT("CoarseSediments"), TEXT("Coarse Sediments"), 0.3, 0.0, 1.0, TEXT("Sediment Discharge")),
			Num(TEXT("CoarseDischargeAngle"), TEXT("Discharge Angle (Coarse)"), 38.0, 0.0, 80.0, TEXT("Sediment Discharge")),
			Num(TEXT("DepositionBoost"), TEXT("Deposition Boost"), 0.2, 0.0, 2.0, TEXT("Sediment Discharge")),
			Num(TEXT("ExtraDepositionBoost"), TEXT("Extra Deposition Boost"), 0.0, 0.0, 2.0, TEXT("Sediment Discharge")),
			Num(TEXT("Shape"), TEXT("Shape"), 0.5, 0.0, 1.0, TEXT("Shape")),
			Num(TEXT("ShapeSharpness"), TEXT("Shape Sharpness"), 0.5, 0.0, 1.0, TEXT("Shape")),
			Num(TEXT("ShapeDetailScale"), TEXT("Shape Detail Scale"), 1.0, 0.1, 8.0, TEXT("Shape")),
			Bool(TEXT("EnableOrographic"), TEXT("Enable"), false, TEXT("Orographic Influence")),
			Num(TEXT("DirectionalPrecipitation"), TEXT("Directional Precipitation"), 0.5, 0.0, 1.0, TEXT("Orographic Influence")),
			Num(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0, TEXT("Orographic Influence")),
			Num(TEXT("RainShadow"), TEXT("Rain Shadow"), 0.5, 0.0, 1.0, TEXT("Orographic Influence")),
			Range(TEXT("Slope"), TEXT("Slope"), 0.0, 90.0, 0.0, 90.0, TEXT("Orographic Influence")),
			Range(TEXT("Altitude"), TEXT("Altitude"), 0.0, 1.0, 0.0, 1.0, TEXT("Orographic Influence")),
			Bool(TEXT("Reverse"), TEXT("Reverse"), false, TEXT("Orographic Influence"))
		};
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateErosion2);
	}

	void RegisterThermal2()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::Thermal2;
		D.DisplayName = TEXT("Thermal2");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Physical-scale thermal weathering that creates talus and slope breakdown.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Talus"), TEXT("Talus")));
		D.Parameters = {
			Int(TEXT("Duration"), TEXT("Duration"), 16, 1, 1024, TEXT("Erosion")),
			Num(TEXT("Strength"), TEXT("Strength"), 0.45, 0.0, 1.0, TEXT("Erosion")),
			Num(TEXT("Anisotropy"), TEXT("Anisotropy"), 0.0, 0.0, 1.0, TEXT("Erosion")),
			Num(TEXT("Angle"), TEXT("Angle"), 34.0, 0.0, 89.9, TEXT("Talus")),
			Num(TEXT("SedimentRemoval"), TEXT("Sediment Removal"), 0.0, 0.0, 1.0, TEXT("Talus")),
			Num(TEXT("FeatureScale"), TEXT("Feature Scale"), 20.0, 0.1, 10000.0, TEXT("Scale"))
		};
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateThermal2);
	}

	void RegisterCrumble()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::Crumble;
		D.DisplayName = TEXT("Crumble");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Collapses terrain from sharp edges and erosive structures with directional and material-resistance controls.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Crumble"), TEXT("Crumble")));
		D.Parameters = {
			Int(TEXT("Duration"), TEXT("Duration"), 8, 1, 128, TEXT("Crumble")),
			Num(TEXT("Strength"), TEXT("Strength"), 0.45, 0.0, 1.0, TEXT("Crumble")),
			Num(TEXT("Coverage"), TEXT("Coverage"), 0.65, 0.0, 1.0, TEXT("Crumble")),
			Num(TEXT("Horizontal"), TEXT("Horizontal"), 0.0, -1.0, 1.0, TEXT("Crumble")),
			Num(TEXT("Vertical"), TEXT("Vertical"), 0.0, -1.0, 1.0, TEXT("Crumble")),
			Num(TEXT("RockHardness"), TEXT("Rock Hardness"), 0.35, 0.0, 1.0, TEXT("Crumble")),
			Num(TEXT("Edge"), TEXT("Edge"), 0.5, 0.0, 1.0, TEXT("Crumble")),
			Num(TEXT("Downcutting"), TEXT("Downcutting"), 0.35, 0.0, 1.0, TEXT("Crumble")),
			Num(TEXT("Depth"), TEXT("Depth"), 0.4, 0.0, 1.0, TEXT("Crumble")),
			Choice(TEXT("Direction"), TEXT("Direction"), TEXT("X"), { TEXT("X"), TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") }, TEXT("Crumble"))
		};
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateCrumble);
	}

	void RegisterHillify()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::Hillify;
		D.DisplayName = TEXT("Hillify");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Softens terrain into broad hill forms with moderate or aggressive creep and optional eroded surface character.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Mask"), TEXT("Mask")));
		D.Parameters = {
			Num(TEXT("Coverage"), TEXT("Coverage"), 0.75, 0.0, 1.0, TEXT("Hillify")),
			Choice(TEXT("Creep"), TEXT("Creep"), TEXT("Moderate"), { TEXT("Moderate"), TEXT("Aggressive") }, TEXT("Hillify")),
			Choice(TEXT("Surface"), TEXT("Surface"), TEXT("Smooth"), { TEXT("Smooth"), TEXT("Eroded") }, TEXT("Hillify")),
			Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Hillify"))
		};
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateHillify);
	}
}

void RegisterGaeaSimulateEvolutionNodes()
{
	using namespace GaeaSimulateEvolution;
	RegisterEasyErosion();
	RegisterErosion2();
	RegisterThermal2();
	RegisterCrumble();
	RegisterHillify();
}
