#include "GaeaTerrainLandformNodes.h"

#include "GaeaGridDomain.h"
#include "GaeaHydraulicErosion.h"
#include "GaeaRidgeNode.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainPhysicalMetrics.h"
#include "GaeaTerrainProceduralOps.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaTerrainValue.h"
#include "Math/RandomStream.h"

namespace
{
	FGaeaTerrainPortDescriptor TerrainIn()
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = TEXT("In");
		P.DisplayName = TEXT("In");
		P.DataType = TEXT("Terrain");
		return P;
	}

	FGaeaTerrainPortDescriptor TerrainOut()
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = TEXT("Out");
		P.DisplayName = TEXT("Out");
		P.DataType = TEXT("Terrain");
		return P;
	}

	FGaeaTerrainPortDescriptor ScalarOut(FName Name, const TCHAR* Label)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = TEXT("ScalarField");
		return P;
	}

	FGaeaTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		return P;
	}

	FGaeaTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = static_cast<double>(Max);
		return P;
	}

	FGaeaTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		return P;
	}

	FGaeaTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName Option : Options) P.NameOptions.Add(Option);
		return P;
	}

	float Smooth01(float Value)
	{
		Value = FMath::Clamp(Value, 0.0f, 1.0f);
		return Value * Value * (3.0f - 2.0f * Value);
	}

	FGaeaGridDomain BuildMountainDomain(const FGaeaTerrainEvaluationContext& Context)
	{
		if (const FGaeaScalarField* SourceHeight = Context.SourceDataset.FindScalarField(GaeaTerrainFieldNames::Height))
		{
			if (SourceHeight->IsValid()) return SourceHeight->Domain;
		}

		const int32 Width = FMath::Clamp(Context.TargetResolution.X > 1 ? Context.TargetResolution.X : 257, 2, 4097);
		const int32 Height = FMath::Clamp(Context.TargetResolution.Y > 1 ? Context.TargetResolution.Y : Width, 2, 4097);
		double WorldWidthCm = 100000.0;
		double WorldDepthCm = 100000.0;
		if (Context.PhysicalMetrics.HasWorldDimensions())
		{
			WorldWidthCm = Context.PhysicalMetrics.WorldWidthMeters * 100.0;
			WorldDepthCm = Context.PhysicalMetrics.WorldDepthMeters * 100.0;
		}
		return FGaeaGridDomain::Make(
			FIntPoint(Width, Height),
			FVector2d(-WorldWidthCm * 0.5, -WorldDepthCm * 0.5),
			FVector2d(WorldWidthCm * 0.5, WorldDepthCm * 0.5));
	}

	float ResolveHeightScale(const FGaeaTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale())
		{
			return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		}
		return FMath::Max(Context.HeightScale, 1.0f);
	}

	FGaeaScalarField MakeHeightField(const FGaeaGridDomain& Domain)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	FGaeaRidgeSettings MakeMountainRidgeSettings(FRandomStream& Random, int32 Layer, int32 Seed)
	{
		FGaeaRidgeSettings Settings;
		Settings.Height = 0.6f;
		Settings.Seed = Seed;
		Settings.ScaleX = 1.0f;
		Settings.ScaleY = 1.0f;

		// These are the three distinct Ridge parameter bands visible in the supplied
		// Mountain implementation. Unknown packed constants remain isolated here.
		switch (Layer)
		{
		case 0:
			Settings.Scale = Random.FRandRange(0.50f, 0.75f);
			Settings.Definition = 0.40f;
			break;
		case 1:
			Settings.Scale = Random.FRandRange(0.25f, 0.50f);
			Settings.Definition = Random.FRandRange(0.50f, 1.00f);
			break;
		default:
			Settings.Scale = Random.FRandRange(0.14f, 0.38f);
			Settings.Definition = Random.FRandRange(0.10f, 0.60f);
			break;
		}
		return Settings;
	}

	bool ApplyMountainPreWarp(FGaeaScalarField& Height, int32 Seed, FString& Error)
	{
		GaeaTerrainProceduralOps::FFractalWarpSettings Settings;
		Settings.Size = 0.5f;
		Settings.Strength = 0.5f;
		Settings.bPersistStrength = true;
		Settings.ZScale = 0.0f;
		Settings.NoiseType = TEXT("Perlin FBM");
		Settings.Perturbation = 0.5f;
		Settings.Octaves = 12;
		Settings.Roughness = 0.5f;
		Settings.bNormalized = false;
		Settings.Iterations = 1;
		Settings.Mode = TEXT("Vector Field");
		Settings.EdgeBehaviour = GaeaTerrainProceduralOps::EEdgeBehaviour::Edge;
		Settings.Seed = Seed;
		Settings.Modulation = 0.0f;
		Settings.ModulationDirectionDegrees = 45.0f;
		Settings.Jitter = 0.45f;

		FGaeaScalarField Warped;
		if (!GaeaTerrainProceduralOps::FractalWarp(Height, Settings, Warped, &Error)) return false;
		for (int32 I = 0; I < Height.Values.Num(); ++I)
		{
			Height.Values[I] = FMath::Min(Height.Values[I], Warped.Values[I]);
		}
		return true;
	}

	bool ApplyStyleErosion(
		FGaeaScalarField& Height,
		FGaeaTerrainDataset& Dataset,
		FName Style,
		bool bReduceDetails,
		int32 Seed,
		float HeightScale,
		const FGaeaTerrainEvaluationContext& Context,
		FString& Error)
	{
		if (Style == TEXT("Basic")) return true;

		const bool bOld = Style == TEXT("Old");
		const bool bAlpineOrStrata = Style == TEXT("Alpine") || Style == TEXT("Strata");
		FGaeaHydraulicErosionSettings Settings;
		Settings.Iterations = bReduceDetails ? 14 : bOld ? 58 : bAlpineOrStrata ? 40 : 30;
		Settings.RockSoftness = bOld ? 0.62f : bAlpineOrStrata ? 0.34f : 0.44f;
		Settings.Strength = bOld ? 0.72f : bAlpineOrStrata ? 0.68f : 0.56f;
		Settings.Downcutting = bOld ? 0.38f : bAlpineOrStrata ? 0.78f : 0.58f;
		Settings.Inhibition = bOld ? 0.08f : bAlpineOrStrata ? 0.02f : 0.05f;
		Settings.BaseLevel = -1.0f;
		Settings.FeatureScale = bOld ? 4.0f : bAlpineOrStrata ? 2.2f : 3.0f;
		Settings.Debris = bOld ? 0.74f : bAlpineOrStrata ? 0.38f : 0.52f;
		Settings.Volume = bOld ? 1.18f : bAlpineOrStrata ? 1.08f : 1.0f;
		Settings.SedimentRemoval = bOld ? 0.08f : bAlpineOrStrata ? 0.30f : 0.14f;
		Settings.Seed = Seed;
		Settings.bAggressiveMode = bAlpineOrStrata;
		Settings.bDeterministic = true;
		Settings.bAdvancedFlowSolver = true;
		Settings.Rainfall = bOld ? 0.015f : bAlpineOrStrata ? 0.012f : 0.010f;
		Settings.FlowRate = bOld ? 0.58f : bAlpineOrStrata ? 0.68f : 0.58f;
		Settings.SedimentCapacity = bOld ? 0.74f : bAlpineOrStrata ? 0.66f : 0.70f;
		Settings.ErosionRate = bOld ? 0.21f : bAlpineOrStrata ? 0.24f : 0.18f;
		Settings.DepositionRate = bOld ? 0.14f : bAlpineOrStrata ? 0.08f : 0.11f;
		Settings.Evaporation = bOld ? 0.07f : bAlpineOrStrata ? 0.10f : 0.08f;
		Settings.MinimumSlope = bOld ? 0.006f : bAlpineOrStrata ? 0.012f : 0.009f;
		Settings.PhysicalSampleSpacingMeters = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(
			Height.Domain.Dimensions,
			Height.Domain.GetCellSize());
		Settings.PhysicalElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(HeightScale);

		FGaeaHydraulicErosionResult Result;
		if (!FGaeaHydraulicErosion::Evaluate(Height, HeightScale, Settings, Result))
		{
			Error = TEXT("Mountain style erosion failed.");
			return false;
		}

		Height = MoveTemp(Result.Height);
		Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
		Result.Wear.Descriptor.Name = GaeaTerrainFieldNames::Wear;
		Result.Deposits.Descriptor.Name = GaeaTerrainFieldNames::Deposits;
		Result.Flow.Descriptor.Name = GaeaTerrainFieldNames::Flow;
		if (!Dataset.SetScalarField(Result.Wear)
			|| !Dataset.SetScalarField(Result.Deposits)
			|| !Dataset.SetScalarField(Result.Flow))
		{
			Error = TEXT("Mountain style erosion could not publish process fields.");
			return false;
		}
		return true;
	}

	void ApplyStrataProfile(FGaeaScalarField& Height, bool bReduceDetails, int32 Seed)
	{
		float MaxHeight = 0.0f;
		for (const float Value : Height.Values) MaxHeight = FMath::Max(MaxHeight, Value);
		if (MaxHeight <= UE_SMALL_NUMBER) return;

		const float Levels = bReduceDetails ? 18.0f : 34.0f;
		FRandomStream Random(Seed + 911);
		const float PhaseOffset = Random.FRandRange(-0.25f, 0.25f);
		for (int32 Y = 0; Y < Height.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height.Domain.Dimensions.X; ++X)
			{
				const float Source = Height.AtInterior(X, Y);
				const float H01 = FMath::Clamp(Source / MaxHeight, 0.0f, 1.0f);
				const float Mask = Smooth01((H01 - 0.18f) / 0.68f);
				const float Phase = H01 * Levels + PhaseOffset;
				const float Terrace = FMath::FloorToFloat(Phase) / Levels;
				const float TerracedHeight = FMath::Clamp(Terrace, 0.0f, 1.0f) * MaxHeight;
				Height.AtInterior(X, Y) = FMath::Lerp(Source, TerracedHeight, Mask * (bReduceDetails ? 0.28f : 0.46f));
			}
		}
	}

	void ApplyBulk(FGaeaScalarField& Height, FName Bulk)
	{
		if (Bulk == TEXT("Medium")) return;
		float MaxHeight = 0.0f;
		for (const float Value : Height.Values) MaxHeight = FMath::Max(MaxHeight, Value);
		if (MaxHeight <= UE_SMALL_NUMBER) return;
		const float Exponent = Bulk == TEXT("Low") ? 1.34f : 0.76f;
		for (float& Value : Height.Values)
		{
			const float H01 = FMath::Clamp(Value / MaxHeight, 0.0f, 1.0f);
			Value = FMath::Pow(H01, Exponent) * MaxHeight;
		}
	}

	bool ApplyInputMultiply(FGaeaScalarField& Height, const FGaeaTerrainNodeInputs& Inputs, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("In"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input) return true;
		if (!Input->IsValid())
		{
			Error = TEXT("Mountain received an invalid In value.");
			return false;
		}

		const FGaeaScalarField* Multiplier = nullptr;
		if (Input->Type == EGaeaTerrainValueType::Terrain) Multiplier = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		else if (Input->Type == EGaeaTerrainValueType::ScalarField) Multiplier = &Input->ScalarField;
		if (!Multiplier || !Multiplier->IsValid())
		{
			Error = TEXT("Mountain In must provide a valid height/scalar field.");
			return false;
		}
		if (Multiplier->Domain != Height.Domain)
		{
			Error = TEXT("Mountain In must share the Mountain evaluation domain.");
			return false;
		}
		for (int32 Y = 0; Y < Height.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height.Domain.Dimensions.X; ++X)
			{
				Height.AtInterior(X, Y) *= Multiplier->AtInterior(X, Y);
			}
		}
		return true;
	}

	bool RebuildSemantics(FGaeaTerrainDataset& Dataset, float HeightScale, const FGaeaTerrainPhysicalMetrics& Metrics, FString& Error)
	{
		if (!FGaeaTerrainDerivedData::EnsureContext(Dataset, HeightScale, Metrics, &Error)) return false;
		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* Slope = Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		const FGaeaScalarField* Concavity = Dataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
		const FGaeaScalarField* Convexity = Dataset.FindScalarField(GaeaTerrainFieldNames::Convexity);
		if (!Height || !Slope || !Concavity || !Convexity)
		{
			Error = TEXT("Mountain could not rebuild semantic fields.");
			return false;
		}

		float MaxHeight = 0.0f;
		for (const float Value : Height->Values) MaxHeight = FMath::Max(MaxHeight, Value);
		const float Denominator = FMath::Max(MaxHeight, UE_SMALL_NUMBER);
		auto MakeField = [Height](FName Name)
		{
			FGaeaScalarField Field = *Height;
			Field.Descriptor.Name = Name;
			Field.Descriptor.Unit = EGaeaFieldUnit::Normalized;
			return Field;
		};

		FGaeaScalarField Mass = MakeField(GaeaTerrainFieldNames::MountainMass);
		FGaeaScalarField Uplift = MakeField(GaeaTerrainFieldNames::Uplift);
		FGaeaScalarField Ridge = MakeField(GaeaTerrainFieldNames::RidgeNetwork);
		FGaeaScalarField Drainage = MakeField(GaeaTerrainFieldNames::DrainageReadiness);
		FGaeaScalarField Erosion = MakeField(GaeaTerrainFieldNames::ErosionEligibility);
		FGaeaScalarField Rock = MakeField(GaeaTerrainFieldNames::RockExposure);
		FGaeaScalarField Cryosphere = MakeField(GaeaTerrainFieldNames::CryosphereEligibility);

		for (int32 Y = 0; Y < Height->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
			{
				const float H = FMath::Clamp(Height->AtInterior(X, Y) / Denominator, 0.0f, 1.0f);
				const float M = Smooth01(H);
				const float S = FMath::Clamp(Slope->AtInterior(X, Y) / 70.0f, 0.0f, 1.0f);
				const float C = FMath::Clamp(Concavity->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
				const float V = FMath::Clamp(Convexity->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
				const float RidgeMass = Smooth01(FMath::Clamp((H - 0.06f) / 0.76f, 0.0f, 1.0f));
				const float R = FMath::Clamp(RidgeMass * (0.24f + V * 0.38f + S * 0.28f + H * 0.10f), 0.0f, 1.0f);
				Mass.AtInterior(X, Y) = M;
				Uplift.AtInterior(X, Y) = H * M;
				Ridge.AtInterior(X, Y) = R;
				Drainage.AtInterior(X, Y) = M * FMath::Clamp(S * 0.52f + C * 0.48f, 0.0f, 1.0f);
				Erosion.AtInterior(X, Y) = M * FMath::Clamp(S * 0.72f + H * 0.28f, 0.0f, 1.0f);
				Rock.AtInterior(X, Y) = M * FMath::Clamp(S * 0.48f + V * 0.30f + H * 0.22f, 0.0f, 1.0f);
				Cryosphere.AtInterior(X, Y) = M * Smooth01((H - 0.58f) / 0.32f) * FMath::Lerp(0.72f, 1.0f, R);
			}
		}

		return Dataset.SetHeightDerivedScalarField(MoveTemp(Mass))
			&& Dataset.SetHeightDerivedScalarField(MoveTemp(Uplift))
			&& Dataset.SetHeightDerivedScalarField(MoveTemp(Ridge))
			&& Dataset.SetHeightDerivedScalarField(MoveTemp(Drainage))
			&& Dataset.SetHeightDerivedScalarField(MoveTemp(Erosion))
			&& Dataset.SetHeightDerivedScalarField(MoveTemp(Rock))
			&& Dataset.SetHeightDerivedScalarField(MoveTemp(Cryosphere));
	}

	bool EvaluateMountain(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const float MountainScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.0001f, 1.0f);
		const float HeightAmount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.25)), 0.0001f, 3.0f);
		const bool bReduceDetails = Node.GetBool(TEXT("ReduceDetails"), false);
		const FName Style = Node.GetName(TEXT("Style"), TEXT("Eroded"));
		const FName Bulk = Node.GetName(TEXT("Bulk"), TEXT("Medium"));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float XCenter = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("X"), 0.5)), 0.0f, 1.0f);
		const float YCenter = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Y"), 0.5)), 0.0f, 1.0f);

		const FGaeaGridDomain Domain = BuildMountainDomain(Context);
		if (!Domain.IsValid())
		{
			Error = TEXT("Mountain produced an invalid evaluation domain.");
			return false;
		}

		FRandomStream RidgeRandom(Seed);
		const FGaeaRidgeSettings RidgeSettings0 = MakeMountainRidgeSettings(RidgeRandom, 0, Seed);
		const FGaeaRidgeSettings RidgeSettings1 = MakeMountainRidgeSettings(RidgeRandom, 1, Seed + 1);
		const FGaeaRidgeSettings RidgeSettings2 = MakeMountainRidgeSettings(RidgeRandom, 2, Seed + 3);

		FGaeaScalarField Ridge0;
		FGaeaScalarField Ridge1;
		FGaeaScalarField Ridge2;
		if (!FGaeaRidgeGenerator::Generate(Domain, RidgeSettings0, Ridge0, &Error)
			|| !FGaeaRidgeGenerator::Generate(Domain, RidgeSettings1, Ridge1, &Error)
			|| !FGaeaRidgeGenerator::Generate(Domain, RidgeSettings2, Ridge2, &Error))
		{
			Error = Error.IsEmpty() ? TEXT("Mountain could not generate its Ridge fields.") : Error;
			return false;
		}

		FGaeaScalarField Height = MakeHeightField(Domain);
		for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Domain.Dimensions.X; ++X)
			{
				Height.AtInterior(X, Y) = Ridge0.AtInterior(X, Y) + Ridge1.AtInterior(X, Y) + Ridge2.AtInterior(X, Y);
			}
		}

		// The supplied Gradients.RadialGradient uses smoothstep radial falloff.
		GaeaTerrainProceduralOps::ApplyRadialGradientMultiply(
			Height,
			static_cast<float>(Domain.Dimensions.X) * XCenter,
			static_cast<float>(Domain.Dimensions.Y) * YCenter,
			static_cast<float>(Domain.Dimensions.X) * MountainScale,
			1.0f);

		// The supplied Mountain implementation applies Min(base, FractalWarp(base))
		// to every style except Old, before multiplying by Height.
		if (Style != TEXT("Old") && !ApplyMountainPreWarp(Height, Seed + 27, Error)) return false;
		for (float& Value : Height.Values) Value *= HeightAmount;

		const float HeightScale = ResolveHeightScale(Context);
		FGaeaTerrainDataset Dataset;
		if (!ApplyStyleErosion(Height, Dataset, Style, bReduceDetails, Seed, HeightScale, Context, Error)) return false;
		if (Style == TEXT("Strata")) ApplyStrataProfile(Height, bReduceDetails, Seed);
		ApplyBulk(Height, Bulk);
		if (!ApplyInputMultiply(Height, Inputs, Error)) return false;

		Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Mountain could not publish Height.");
			return false;
		}
		if (!RebuildSemantics(Dataset, HeightScale, Context.PhysicalMetrics, Error)) return false;

		auto Publish = [&](FName OutputName, FName FieldName)
		{
			if (const FGaeaScalarField* Field = Dataset.FindScalarField(FieldName))
			{
				Out.Outputs.Add(OutputName, FGaeaTerrainValue::MakeScalarField(*Field));
			}
		};
		Publish(TEXT("Mass"), GaeaTerrainFieldNames::MountainMass);
		Publish(TEXT("Uplift"), GaeaTerrainFieldNames::Uplift);
		Publish(TEXT("Ridges"), GaeaTerrainFieldNames::RidgeNetwork);
		Publish(TEXT("DrainageReadiness"), GaeaTerrainFieldNames::DrainageReadiness);
		Publish(TEXT("ErosionEligibility"), GaeaTerrainFieldNames::ErosionEligibility);
		Publish(TEXT("RockExposure"), GaeaTerrainFieldNames::RockExposure);
		Publish(TEXT("CryosphereEligibility"), GaeaTerrainFieldNames::CryosphereEligibility);

		FGaeaTerrainValue Terrain = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Terrain.IsValid())
		{
			Error = TEXT("Mountain produced invalid terrain.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Terrain));
		Error.Reset();
		return true;
	}
}

void RegisterGaeaTerrainLandformNodes()
{
	FGaeaTerrainNodeDescriptor D;
	D.Type = GaeaTerrainNodeTypes::Mountain;
	D.DisplayName = TEXT("Mountain");
	D.Category = TEXT("Terrain");
	D.Description = TEXT("Mountain generator built from three shared Ridge fields, smooth radial footprint, fractal warp, style processing, and bulk shaping.");
	D.Inputs = { TerrainIn() };
	D.Outputs = {
		TerrainOut(),
		ScalarOut(TEXT("Mass"), TEXT("Mass")),
		ScalarOut(TEXT("Uplift"), TEXT("Uplift")),
		ScalarOut(TEXT("Ridges"), TEXT("Ridges")),
		ScalarOut(TEXT("DrainageReadiness"), TEXT("Drainage Readiness")),
		ScalarOut(TEXT("ErosionEligibility"), TEXT("Erosion Eligibility")),
		ScalarOut(TEXT("RockExposure"), TEXT("Rock Exposure")),
		ScalarOut(TEXT("CryosphereEligibility"), TEXT("Cryosphere Eligibility"))
	};
	D.Parameters.Add(Num(TEXT("Scale"), TEXT("Scale"), 0.5, 0.0001, 1.0, TEXT("Mountain")));
	D.Parameters.Add(Num(TEXT("Height"), TEXT("Height"), 1.25, 0.0001, 3.0, TEXT("Mountain")));
	D.Parameters.Add(Choice(TEXT("Style"), TEXT("Style"), TEXT("Eroded"), { TEXT("Basic"), TEXT("Eroded"), TEXT("Old"), TEXT("Alpine"), TEXT("Strata") }, TEXT("Mountain")));
	D.Parameters.Add(Choice(TEXT("Bulk"), TEXT("Bulk"), TEXT("Medium"), { TEXT("Low"), TEXT("Medium"), TEXT("High") }, TEXT("Mountain")));
	D.Parameters.Add(Bool(TEXT("ReduceDetails"), TEXT("Reduce Details"), false, TEXT("Mountain")));
	D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Mountain")));
	D.Parameters.Add(Num(TEXT("X"), TEXT("X"), 0.5, 0.0, 1.0, TEXT("Position")));
	D.Parameters.Add(Num(TEXT("Y"), TEXT("Y"), 0.5, 0.0, 1.0, TEXT("Position")));
	FGaeaTerrainNodeDescriptorRegistry::Register(D);
	FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateMountain);
}
