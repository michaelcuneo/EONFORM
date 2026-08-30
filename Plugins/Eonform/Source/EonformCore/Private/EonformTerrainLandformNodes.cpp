#include "EonformTerrainLandformNodes.h"

#include "EonformGridDomain.h"
#include "EonformHydraulicErosion.h"
#include "EonformMountainRegional.h"
#include "EonformRidgeNode.h"
#include "EonformScalarField.h"
#include "EonformTerrainDerivedData.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainFractalWarp.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainPhysicalMetrics.h"
#include "EonformTerrainProceduralOps.h"
#include "EonformTerrainRecipe.h"
#include "EonformTerrainValue.h"
#include "Math/RandomStream.h"

namespace
{
	FEonformTerrainPortDescriptor TerrainIn()
	{
		FEonformTerrainPortDescriptor P;
		P.Name = TEXT("In");
		P.DisplayName = TEXT("In");
		P.DataType = TEXT("Terrain");
		return P;
	}

	FEonformTerrainPortDescriptor TerrainOut()
	{
		FEonformTerrainPortDescriptor P;
		P.Name = TEXT("Out");
		P.DisplayName = TEXT("Out");
		P.DataType = TEXT("Terrain");
		return P;
	}

	FEonformTerrainPortDescriptor ScalarOut(FName Name, const TCHAR* Label)
	{
		FEonformTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = TEXT("ScalarField");
		return P;
	}

	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EEonformTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		return P;
	}

	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EEonformTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = Max;
		return P;
	}

	FEonformTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EEonformTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		return P;
	}

	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EEonformTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName Option : Options) P.NameOptions.Add(Option);
		return P;
	}

	float Smooth01(float Value)
	{
		Value = FMath::Clamp(Value, 0.0f, 1.0f);
		return Value * Value * (3.0f - 2.0f * Value);
	}

	FEonformGridDomain BuildMountainDomain(const FEonformTerrainEvaluationContext& Context)
	{
		if (const FEonformScalarField* SourceHeight = Context.SourceDataset.FindScalarField(EonformTerrainFieldNames::Height))
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
		return FEonformGridDomain::Make(
			FIntPoint(Width, Height),
			FVector2d(-WorldWidthCm * 0.5, -WorldDepthCm * 0.5),
			FVector2d(WorldWidthCm * 0.5, WorldDepthCm * 0.5));
	}

	FIntPoint ResolveMountainReferenceResolution(const FEonformTerrainEvaluationContext& Context)
	{
		const FIntPoint Requested = Context.ReferenceResolution.X > 1 && Context.ReferenceResolution.Y > 1
			? Context.ReferenceResolution
			: Context.TargetResolution;
		const int32 Width = FMath::Clamp(Requested.X > 1 ? Requested.X : 257, 2, 4097);
		const int32 Height = FMath::Clamp(Requested.Y > 1 ? Requested.Y : Width, 2, 4097);
		return FIntPoint(Width, Height);
	}

	FEonformGridDomain BuildMountainReferenceDomain(const FEonformTerrainEvaluationContext& Context)
	{
		const FIntPoint Dimensions = ResolveMountainReferenceResolution(Context);
		const FEonformGridDomain Reference = Context.ResolveReferenceDomain(
			Dimensions,
			FVector2d(-50000.0, -50000.0),
			FVector2d(50000.0, 50000.0));
		if (!Reference.IsValid()) return FEonformGridDomain();
		return FEonformGridDomain::Make(Dimensions, Reference.WorldMin, Reference.WorldMax);
	}

	FEonformGridDomain BuildMountainTargetDomain(
		const FEonformTerrainEvaluationContext& Context,
		const FEonformGridDomain& ReferenceDomain)
	{
		FIntPoint Dimensions = Context.TargetResolution;
		if (Dimensions.X < 2 || Dimensions.Y < 2) Dimensions = ReferenceDomain.Dimensions;
		if (Context.HasRegion())
		{
			return FEonformGridDomain::Make(
				Dimensions,
				Context.Region.WorldMinCm,
				Context.Region.WorldMaxCm,
				Context.Region.BorderSamples);
		}
		return FEonformGridDomain::Make(Dimensions, ReferenceDomain.WorldMin, ReferenceDomain.WorldMax);
	}

	float ResolveHeightScale(const FEonformTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale())
		{
			return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		}
		return FMath::Max(Context.HeightScale, 1.0f);
	}

	FEonformScalarField MakeHeightField(const FEonformGridDomain& Domain)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = EonformTerrainFieldNames::Height;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	FEonformRidgeSettings MakeMountainRidgeSettings(FRandomStream& Random, int32 Layer, int32 Seed)
	{
		FEonformRidgeSettings Settings;
		Settings.Height = 0.6f;
		Settings.Seed = Seed;
		Settings.ScaleX = 1.0f;
		Settings.ScaleY = 1.0f;

		// These are the three distinct Ridge parameter bands recovered from the
		// Mountain implementation. Keep the coarse-to-fine construction intact.
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

	bool ApplyMountainPreWarp(FEonformScalarField& Height, int32 Seed, FString& Error)
	{
		EonformTerrainProceduralOps::FFractalWarpSettings Settings;
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
		Settings.EdgeBehaviour = EonformTerrainProceduralOps::EEdgeBehaviour::Edge;
		Settings.Seed = Seed;
		Settings.Modulation = 0.0f;
		Settings.ModulationDirectionDegrees = 45.0f;
		Settings.Jitter = 0.45f;

		FEonformScalarField Warped;
		if (!EonformTerrainProceduralOps::FractalWarpFidelity(Height, Settings, Warped, &Error)) return false;
		for (int32 I = 0; I < Height.Values.Num(); ++I)
		{
			Height.Values[I] = FMath::Min(Height.Values[I], Warped.Values[I]);
		}
		return true;
	}

	bool ApplyStyleErosion(
		FEonformScalarField& Height,
		FEonformTerrainDataset& Dataset,
		FName Style,
		bool bReduceDetails,
		int32 Seed,
		float HeightScale,
		const FEonformTerrainEvaluationContext& Context,
		FString& Error)
	{
		if (Style == TEXT("Basic")) return true;

		const bool bOld = Style == TEXT("Old");
		const bool bAlpineOrStrata = Style == TEXT("Alpine") || Style == TEXT("Strata");
		FEonformHydraulicErosionSettings Settings;
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

		FEonformHydraulicErosionResult Result;
		if (!FEonformHydraulicErosion::Evaluate(Height, HeightScale, Settings, Result))
		{
			Error = TEXT("Mountain style erosion failed.");
			return false;
		}

		Height = MoveTemp(Result.Height);
		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		Result.Wear.Descriptor.Name = EonformTerrainFieldNames::Wear;
		Result.Deposits.Descriptor.Name = EonformTerrainFieldNames::Deposits;
		Result.Flow.Descriptor.Name = EonformTerrainFieldNames::Flow;
		if (!Dataset.SetScalarField(Result.Wear)
			|| !Dataset.SetScalarField(Result.Deposits)
			|| !Dataset.SetScalarField(Result.Flow))
		{
			Error = TEXT("Mountain style erosion could not publish process fields.");
			return false;
		}
		return true;
	}

	void ApplyStrataProfile(FEonformScalarField& Height, bool bReduceDetails, int32 Seed)
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

	void ApplyBulk(FEonformScalarField& Height, FName Bulk)
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

	bool ApplyInputMultiply(FEonformScalarField& Height, const FEonformTerrainNodeInputs& Inputs, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("In"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input) return true;
		if (!Input->IsValid())
		{
			Error = TEXT("Mountain received an invalid In value.");
			return false;
		}

		const FEonformScalarField* Multiplier = nullptr;
		if (Input->Type == EEonformTerrainValueType::Terrain) Multiplier = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		else if (Input->Type == EEonformTerrainValueType::ScalarField) Multiplier = &Input->ScalarField;
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

	bool RebuildSemantics(FEonformTerrainDataset& Dataset, float HeightScale, const FEonformTerrainPhysicalMetrics& Metrics, FString& Error)
	{
		if (!FEonformTerrainDerivedData::EnsureContext(Dataset, HeightScale, Metrics, &Error)) return false;
		const FEonformScalarField* Height = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		const FEonformScalarField* Slope = Dataset.FindScalarField(EonformTerrainFieldNames::SlopeDegrees);
		const FEonformScalarField* Concavity = Dataset.FindScalarField(EonformTerrainFieldNames::Concavity);
		const FEonformScalarField* Convexity = Dataset.FindScalarField(EonformTerrainFieldNames::Convexity);
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
			FEonformScalarField Field = *Height;
			Field.Descriptor.Name = Name;
			Field.Descriptor.Unit = EEonformFieldUnit::Normalized;
			return Field;
		};

		FEonformScalarField Mass = MakeField(EonformTerrainFieldNames::MountainMass);
		FEonformScalarField Uplift = MakeField(EonformTerrainFieldNames::Uplift);
		FEonformScalarField Ridge = MakeField(EonformTerrainFieldNames::RidgeNetwork);
		FEonformScalarField Drainage = MakeField(EonformTerrainFieldNames::DrainageReadiness);
		FEonformScalarField Erosion = MakeField(EonformTerrainFieldNames::ErosionEligibility);
		FEonformScalarField Rock = MakeField(EonformTerrainFieldNames::RockExposure);
		FEonformScalarField Cryosphere = MakeField(EonformTerrainFieldNames::CryosphereEligibility);

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
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
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

		if (Context.HasRegion() && (Style != TEXT("Basic") || Bulk != TEXT("Medium")))
		{
			Error = TEXT("Regional Mountain currently requires Style=Basic and Bulk=Medium; erosion, strata, and global bulk reductions remain fail-closed.");
			return false;
		}

		FRandomStream RidgeRandom(Seed);
		const FEonformRidgeSettings RidgeSettings0 = MakeMountainRidgeSettings(RidgeRandom, 0, Seed);
		const FEonformRidgeSettings RidgeSettings1 = MakeMountainRidgeSettings(RidgeRandom, 1, Seed + 1);
		const FEonformRidgeSettings RidgeSettings2 = MakeMountainRidgeSettings(RidgeRandom, 2, Seed + 3);

		FEonformScalarField Height;
		if (Context.HasRegion())
		{
			const FEonformGridDomain ReferenceDomain = BuildMountainReferenceDomain(Context);
			const FEonformGridDomain TargetDomain = BuildMountainTargetDomain(Context, ReferenceDomain);
			if (!ReferenceDomain.IsValid() || !TargetDomain.IsValid())
			{
				Error = TEXT("Mountain produced an invalid regional evaluation domain.");
				return false;
			}
			if (!EonformMountainRegional::GenerateCore(
				TargetDomain,
				ReferenceDomain,
				RidgeSettings0,
				RidgeSettings1,
				RidgeSettings2,
				MountainScale,
				XCenter,
				YCenter,
				Seed + 27,
				Style != TEXT("Old"),
				Node.Id,
				Context.GlobalSummaryCache,
				Height,
				&Error)) return false;
		}
		else
		{
			const FEonformGridDomain Domain = BuildMountainDomain(Context);
			if (!Domain.IsValid())
			{
				Error = TEXT("Mountain produced an invalid evaluation domain.");
				return false;
			}

			FEonformScalarField Ridge0;
			FEonformScalarField Ridge1;
			FEonformScalarField Ridge2;
			if (!FEonformRidgeGenerator::Generate(Domain, RidgeSettings0, Ridge0, &Error)
				|| !FEonformRidgeGenerator::Generate(Domain, RidgeSettings1, Ridge1, &Error)
				|| !FEonformRidgeGenerator::Generate(Domain, RidgeSettings2, Ridge2, &Error))
			{
				Error = Error.IsEmpty() ? TEXT("Mountain could not generate its Ridge fields.") : Error;
				return false;
			}

			Height = MakeHeightField(Domain);
			for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Domain.Dimensions.X; ++X)
				{
					Height.AtInterior(X, Y) = Ridge0.AtInterior(X, Y) + Ridge1.AtInterior(X, Y) + Ridge2.AtInterior(X, Y);
				}
			}

			EonformTerrainProceduralOps::ApplyRadialGradientMultiply(
				Height,
				static_cast<float>(Domain.Dimensions.X) * XCenter,
				static_cast<float>(Domain.Dimensions.Y) * YCenter,
				static_cast<float>(Domain.Dimensions.X) * MountainScale,
				1.0f);

			if (Style != TEXT("Old") && !ApplyMountainPreWarp(Height, Seed + 27, Error)) return false;
		}

		for (float& Value : Height.Values) Value *= HeightAmount;

		const float HeightScale = ResolveHeightScale(Context);
		FEonformTerrainDataset Dataset;
		if (!ApplyStyleErosion(Height, Dataset, Style, bReduceDetails, Seed, HeightScale, Context, Error)) return false;
		if (Style == TEXT("Strata")) ApplyStrataProfile(Height, bReduceDetails, Seed);
		ApplyBulk(Height, Bulk);
		if (!ApplyInputMultiply(Height, Inputs, Error)) return false;

		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Mountain could not publish Height.");
			return false;
		}
		if (!RebuildSemantics(Dataset, HeightScale, Context.PhysicalMetrics, Error)) return false;

		auto Publish = [&](FName OutputName, FName FieldName)
		{
			if (const FEonformScalarField* Field = Dataset.FindScalarField(FieldName))
			{
				Out.Outputs.Add(OutputName, FEonformTerrainValue::MakeScalarField(*Field));
			}
		};
		Publish(TEXT("Mass"), EonformTerrainFieldNames::MountainMass);
		Publish(TEXT("Uplift"), EonformTerrainFieldNames::Uplift);
		Publish(TEXT("Ridges"), EonformTerrainFieldNames::RidgeNetwork);
		Publish(TEXT("DrainageReadiness"), EonformTerrainFieldNames::DrainageReadiness);
		Publish(TEXT("ErosionEligibility"), EonformTerrainFieldNames::ErosionEligibility);
		Publish(TEXT("RockExposure"), EonformTerrainFieldNames::RockExposure);
		Publish(TEXT("CryosphereEligibility"), EonformTerrainFieldNames::CryosphereEligibility);

		FEonformTerrainValue Terrain = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
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

void RegisterEonformTerrainLandformNodes()
{
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Mountain;
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
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(D.Type, EvaluateMountain);
}
