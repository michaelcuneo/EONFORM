#include "GaeaTerrainLandformNodes.h"

#include "GaeaErosionNode.h"
#include "GaeaModifySpatialNodes.h"
#include "GaeaRecurveNode.h"
#include "GaeaShaperNode.h"
#include "GaeaSharpenNode.h"
#include "GaeaSurfaceNodes.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainLandformOps.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaThermalErosionNode.h"

namespace
{
	FGaeaTerrainPortDescriptor TerrainOut()
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = TEXT("Out");
		Port.DisplayName = TEXT("Out");
		Port.DataType = TEXT("Terrain");
		return Port;
	}

	FGaeaTerrainPortDescriptor ScalarOut(FName Name, const TCHAR* Label)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = Label;
		Port.DataType = TEXT("ScalarField");
		return Port;
	}

	FGaeaTerrainParameterDescriptor Number(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
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

	FGaeaTerrainParameterDescriptor Integer(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
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

	FGaeaTerrainParameterDescriptor Boolean(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		return P;
	}

	FGaeaTerrainParameterDescriptor Name(FName NameValue, const TCHAR* Label, FName Default, std::initializer_list<FName> Options, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = NameValue;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName Option : Options) P.NameOptions.Add(Option);
		return P;
	}

	float Smooth01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	FGuid CompositeGuid(int32 Seed, uint32 Ordinal)
	{
		return FGuid(0x4D544E00u + Ordinal, 0x454F4E46u, static_cast<uint32>(Seed), 0x434F4D50u);
	}

	FGaeaTerrainNode& AddCompositeNode(FGaeaTerrainRecipe& Recipe, FName Type, int32 Seed, uint32 Ordinal)
	{
		FGaeaTerrainNode Node;
		Node.Id = CompositeGuid(Seed, Ordinal);
		Node.Type = Type;
		return Recipe.Nodes.Add_GetRef(MoveTemp(Node));
	}

	void Connect(
		FGaeaTerrainRecipe& Recipe,
		const FGaeaTerrainNode& From,
		FName FromOutput,
		const FGaeaTerrainNode& To,
		FName ToInput)
	{
		FGaeaTerrainConnection Connection;
		Connection.FromNode = From.Id;
		Connection.FromOutput = FromOutput;
		Connection.ToNode = To.Id;
		Connection.ToInput = ToInput;
		Recipe.Connections.Add(Connection);
	}

	void EnsureMountainCompositeNodeEvaluators()
	{
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Shaper)) RegisterGaeaShaperNode();
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Warp)) RegisterGaeaWarpNode();
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::SlopeWarp)) RegisterGaeaSlopeWarpNode();
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Craggy)) RegisterGaeaSurfaceNodes();
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::HydraulicErosion)) RegisterGaeaErosionNode();
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::ThermalErosion)) RegisterGaeaThermalErosionNode();
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Recurve)) RegisterGaeaRecurveNode();
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Sharpen)) RegisterGaeaSharpenNode();
	}

	bool NormalizeAndRefreshMountainSemantics(
		FGaeaTerrainDataset& Dataset,
		const FGaeaScalarField& RawHeight,
		const FGaeaScalarField& RawMass,
		float RequestedHeight,
		float HeightScale,
		const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
		FString& Error)
	{
		const FGaeaScalarField* ProcessedHeight = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!ProcessedHeight || !ProcessedHeight->IsValid()
			|| ProcessedHeight->Domain != RawHeight.Domain
			|| RawMass.Domain != RawHeight.Domain)
		{
			Error = TEXT("Mountain composite produced an invalid or incompatible Height field.");
			return false;
		}

		FGaeaScalarField FinalHeight = *ProcessedHeight;
		float MaxInside = UE_SMALL_NUMBER;
		for (int32 Y = 0; Y < FinalHeight.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < FinalHeight.Domain.Dimensions.X; ++X)
			{
				const float Mask = Smooth01(RawMass.AtInterior(X, Y));
				const float Blended = FMath::Lerp(RawHeight.AtInterior(X, Y), FinalHeight.AtInterior(X, Y), Mask);
				FinalHeight.AtInterior(X, Y) = Blended;
				if (Mask > 0.08f) MaxInside = FMath::Max(MaxInside, Blended);
			}
		}

		// Preserve all the nested erosion detail while guaranteeing that Height is
		// an actual target summit. This avoids both the old flat saturation cap and
		// erosion passes accidentally shrinking a mountain into a hill.
		const float PeakScale = MaxInside > UE_SMALL_NUMBER
			? FMath::Clamp(RequestedHeight, 0.0f, 1.0f) / MaxInside
			: 1.0f;
		for (int32 Y = 0; Y < FinalHeight.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < FinalHeight.Domain.Dimensions.X; ++X)
			{
				const float Mask = Smooth01(RawMass.AtInterior(X, Y));
				const float Scaled = FinalHeight.AtInterior(X, Y) * PeakScale;
				FinalHeight.AtInterior(X, Y) = FMath::Lerp(
					RawHeight.AtInterior(X, Y),
					FMath::Clamp(Scaled, 0.0f, RequestedHeight),
					Mask);
			}
		}
		FinalHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;
		if (!Dataset.SetScalarField(MoveTemp(FinalHeight)))
		{
			Error = TEXT("Mountain composite could not publish its normalized final Height.");
			return false;
		}

		if (!FGaeaTerrainDerivedData::EnsureContext(Dataset, HeightScale, PhysicalMetrics, &Error)) return false;
		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* Slope = Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		const FGaeaScalarField* Concavity = Dataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
		const FGaeaScalarField* Convexity = Dataset.FindScalarField(GaeaTerrainFieldNames::Convexity);
		if (!Height || !Slope || !Concavity || !Convexity)
		{
			Error = TEXT("Mountain composite could not rebuild final terrain context.");
			return false;
		}

		auto Semantic = [Height](FName FieldName)
		{
			FGaeaScalarField Field = *Height;
			Field.Descriptor.Name = FieldName;
			Field.Descriptor.Unit = EGaeaFieldUnit::Normalized;
			return Field;
		};

		FGaeaScalarField Mass = Semantic(GaeaTerrainFieldNames::MountainMass);
		FGaeaScalarField Uplift = Semantic(GaeaTerrainFieldNames::Uplift);
		FGaeaScalarField Ridges = Semantic(GaeaTerrainFieldNames::RidgeNetwork);
		FGaeaScalarField Drainage = Semantic(GaeaTerrainFieldNames::DrainageReadiness);
		FGaeaScalarField Erosion = Semantic(GaeaTerrainFieldNames::ErosionEligibility);
		FGaeaScalarField Rock = Semantic(GaeaTerrainFieldNames::RockExposure);
		FGaeaScalarField Cryosphere = Semantic(GaeaTerrainFieldNames::CryosphereEligibility);

		const float HeightDenominator = FMath::Max(RequestedHeight, UE_SMALL_NUMBER);
		for (int32 Y = 0; Y < Height->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
			{
				const float M = Smooth01(RawMass.AtInterior(X, Y));
				const float H = FMath::Clamp(Height->AtInterior(X, Y) / HeightDenominator, 0.0f, 1.0f);
				const float S = FMath::Clamp(Slope->AtInterior(X, Y) / 70.0f, 0.0f, 1.0f);
				const float C = FMath::Clamp(Concavity->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
				const float V = FMath::Clamp(Convexity->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
				const float Ridge = M * FMath::Clamp(V * 0.60f + S * 0.40f, 0.0f, 1.0f);

				Mass.AtInterior(X, Y) = M;
				Uplift.AtInterior(X, Y) = H * M;
				Ridges.AtInterior(X, Y) = Ridge;
				Drainage.AtInterior(X, Y) = M * FMath::Clamp(S * 0.62f + C * 0.38f, 0.0f, 1.0f);
				Erosion.AtInterior(X, Y) = M * FMath::Clamp(S * 0.70f + H * 0.30f, 0.0f, 1.0f);
				Rock.AtInterior(X, Y) = M * FMath::Clamp(S * 0.46f + V * 0.29f + H * 0.25f, 0.0f, 1.0f);
				Cryosphere.AtInterior(X, Y) = M * Smooth01((H - 0.58f) / 0.32f) * FMath::Lerp(0.72f, 1.0f, Ridge);
			}
		}

		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Mass))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Uplift))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Ridges))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Drainage))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Erosion))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Rock))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Cryosphere)))
		{
			Error = TEXT("Mountain composite could not publish final semantic fields.");
			return false;
		}
		return true;
	}

	bool RunMountainComposite(
		const FGaeaMountainLandformSettings& Settings,
		const FGaeaTerrainEvaluationContext& OuterContext,
		FGaeaMountainLandformResult& InOutResult,
		FString& Error)
	{
		const FGaeaScalarField* RawHeightPtr = InOutResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* RawMassPtr = InOutResult.Dataset.FindScalarField(GaeaTerrainFieldNames::MountainMass);
		if (!RawHeightPtr || !RawMassPtr)
		{
			Error = TEXT("Mountain base did not expose Height and MountainMass for composite processing.");
			return false;
		}
		const FGaeaScalarField RawHeight = *RawHeightPtr;
		const FGaeaScalarField RawMass = *RawMassPtr;

		EnsureMountainCompositeNodeEvaluators();

		FGaeaTerrainRecipe Recipe;
		uint32 Ordinal = 1;
		FGaeaTerrainNode& Source = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::SourceDataset, Settings.Seed, Ordinal++);

		FGaeaTerrainNode& Shaper = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::Shaper, Settings.Seed, Ordinal++);
		Shaper.NumericParameters.Add(TEXT("Shape"), Settings.Style == TEXT("Old") ? 0.10 : 0.28);
		Shaper.NumericParameters.Add(TEXT("LocalEffect"), 0.42);
		Shaper.NumericParameters.Add(TEXT("LocalArea"), 0.58);
		Shaper.BoolParameters.Add(TEXT("MaintainFineDetails"), true);
		Shaper.NumericParameters.Add(TEXT("DetailSize"), Settings.bReduceDetails ? 0.45 : 0.24);
		Connect(Recipe, Source, TEXT("Terrain"), Shaper, TEXT("Terrain"));

		FGaeaTerrainNode& Warp = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::Warp, Settings.Seed, Ordinal++);
		Warp.NumericParameters.Add(TEXT("Size"), Settings.Style == TEXT("Alpine") ? 0.28 : 0.36);
		Warp.NumericParameters.Add(TEXT("Strength"), Settings.Style == TEXT("Old") ? 0.08 : 0.16);
		Warp.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 101);
		Connect(Recipe, Shaper, TEXT("Out"), Warp, TEXT("Input"));

		FGaeaTerrainNode& SlopeWarp = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::SlopeWarp, Settings.Seed, Ordinal++);
		SlopeWarp.NumericParameters.Add(TEXT("Intensity"), Settings.Style == TEXT("Alpine") ? 0.19 : 0.12);
		SlopeWarp.IntegerParameters.Add(TEXT("Iterations"), Settings.bReduceDetails ? 1 : 2);
		SlopeWarp.NumericParameters.Add(TEXT("Direction"), 17.0);
		SlopeWarp.BoolParameters.Add(TEXT("Normalized"), true);
		Connect(Recipe, Warp, TEXT("Out"), SlopeWarp, TEXT("Input"));

		FGaeaTerrainNode& Craggy = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::Craggy, Settings.Seed, Ordinal++);
		Craggy.NumericParameters.Add(TEXT("Strength"), Settings.Style == TEXT("Old") ? 0.32 : (Settings.Style == TEXT("Alpine") ? 0.86 : 0.62));
		Craggy.NumericParameters.Add(TEXT("Scale"), Settings.bReduceDetails ? 1.8 : 1.05);
		Craggy.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 211);
		Connect(Recipe, SlopeWarp, TEXT("Out"), Craggy, TEXT("Terrain"));

		FGaeaTerrainNode& MacroErosion = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::HydraulicErosion, Settings.Seed, Ordinal++);
		MacroErosion.IntegerParameters.Add(TEXT("Duration"), Settings.bReduceDetails ? 14 : (Settings.Style == TEXT("Eroded") ? 42 : 30));
		MacroErosion.NumericParameters.Add(TEXT("RockSoftness"), Settings.Style == TEXT("Old") ? 0.42 : 0.18);
		MacroErosion.NumericParameters.Add(TEXT("Strength"), Settings.Style == TEXT("Eroded") ? 1.45 : 1.05);
		MacroErosion.NumericParameters.Add(TEXT("Downcutting"), Settings.Style == TEXT("Alpine") ? 0.92 : 0.68);
		MacroErosion.NumericParameters.Add(TEXT("Inhibition"), 0.08);
		MacroErosion.NumericParameters.Add(TEXT("BaseLevel"), 0.0);
		MacroErosion.NumericParameters.Add(TEXT("FeatureScale"), Settings.Style == TEXT("Alpine") ? 1.45 : 1.85);
		MacroErosion.BoolParameters.Add(TEXT("RealScale"), true);
		MacroErosion.NumericParameters.Add(TEXT("TerrainScale"), 1.0);
		MacroErosion.NumericParameters.Add(TEXT("Verticality"), Settings.Style == TEXT("Alpine") ? 1.18 : 1.0);
		MacroErosion.NumericParameters.Add(TEXT("Debris"), 0.46);
		MacroErosion.NumericParameters.Add(TEXT("Volume"), 1.15);
		MacroErosion.NumericParameters.Add(TEXT("SedimentRemoval"), Settings.Style == TEXT("Eroded") ? 0.28 : 0.12);
		MacroErosion.NameParameters.Add(TEXT("AreaEffect"), TEXT("None"));
		MacroErosion.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 307);
		MacroErosion.BoolParameters.Add(TEXT("AggressiveMode"), Settings.Style == TEXT("Alpine"));
		MacroErosion.BoolParameters.Add(TEXT("Deterministic"), true);
		Connect(Recipe, Craggy, TEXT("Out"), MacroErosion, TEXT("Terrain"));

		const FName GeologicalDetailType = Settings.Style == TEXT("Strata")
			? GaeaTerrainNodeTypes::Stratify
			: GaeaTerrainNodeTypes::RockNoise;
		FGaeaTerrainNode& GeologicalDetail = AddCompositeNode(Recipe, GeologicalDetailType, Settings.Seed, Ordinal++);
		GeologicalDetail.NumericParameters.Add(TEXT("Strength"), Settings.Style == TEXT("Old") ? 0.26 : 0.52);
		GeologicalDetail.NumericParameters.Add(TEXT("Scale"), Settings.bReduceDetails ? 1.8 : 0.82);
		GeologicalDetail.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 401);
		if (GeologicalDetailType == GaeaTerrainNodeTypes::Stratify)
		{
			GeologicalDetail.NumericParameters.Add(TEXT("Layers"), Settings.bReduceDetails ? 14.0 : 26.0);
		}
		Connect(Recipe, MacroErosion, TEXT("Out"), GeologicalDetail, TEXT("Terrain"));

		FGaeaTerrainNode* Last = &GeologicalDetail;
		if (!Settings.bReduceDetails)
		{
			FGaeaTerrainNode& MicroErosion = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::HydraulicErosion, Settings.Seed, Ordinal++);
			MicroErosion.IntegerParameters.Add(TEXT("Duration"), Settings.Style == TEXT("Eroded") ? 22 : 14);
			MicroErosion.NumericParameters.Add(TEXT("RockSoftness"), 0.22);
			MicroErosion.NumericParameters.Add(TEXT("Strength"), Settings.Style == TEXT("Alpine") ? 0.92 : 0.72);
			MicroErosion.NumericParameters.Add(TEXT("Downcutting"), Settings.Style == TEXT("Alpine") ? 1.12 : 0.82);
			MicroErosion.NumericParameters.Add(TEXT("Inhibition"), 0.04);
			MicroErosion.NumericParameters.Add(TEXT("BaseLevel"), 0.0);
			MicroErosion.NumericParameters.Add(TEXT("FeatureScale"), 0.48);
			MicroErosion.BoolParameters.Add(TEXT("RealScale"), true);
			MicroErosion.NumericParameters.Add(TEXT("TerrainScale"), 1.0);
			MicroErosion.NumericParameters.Add(TEXT("Verticality"), 1.0);
			MicroErosion.NumericParameters.Add(TEXT("Debris"), 0.32);
			MicroErosion.NumericParameters.Add(TEXT("Volume"), 0.72);
			MicroErosion.NumericParameters.Add(TEXT("SedimentRemoval"), 0.18);
			MicroErosion.NameParameters.Add(TEXT("AreaEffect"), TEXT("None"));
			MicroErosion.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 503);
			MicroErosion.BoolParameters.Add(TEXT("AggressiveMode"), false);
			MicroErosion.BoolParameters.Add(TEXT("Deterministic"), true);
			Connect(Recipe, GeologicalDetail, TEXT("Out"), MicroErosion, TEXT("Terrain"));
			Last = &MicroErosion;
		}

		FGaeaTerrainNode& Thermal = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::ThermalErosion, Settings.Seed, Ordinal++);
		Thermal.IntegerParameters.Add(TEXT("Duration"), Settings.bReduceDetails ? 8 : (Settings.Style == TEXT("Old") ? 24 : 15));
		Thermal.NumericParameters.Add(TEXT("Strength"), Settings.Style == TEXT("Old") ? 0.48 : 0.30);
		Thermal.NumericParameters.Add(TEXT("Anisotropy"), Settings.Style == TEXT("Alpine") ? 0.18 : 0.06);
		Thermal.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 607);
		Thermal.NumericParameters.Add(TEXT("Angle"), Settings.Style == TEXT("Alpine") ? 39.0 : 34.0);
		Thermal.NumericParameters.Add(TEXT("Settling"), Settings.Style == TEXT("Old") ? 0.72 : 0.48);
		Thermal.NumericParameters.Add(TEXT("SedimentRemoval"), Settings.Style == TEXT("Alpine") ? 0.18 : 0.06);
		Thermal.NumericParameters.Add(TEXT("FeatureScale"), 0.82);
		Thermal.BoolParameters.Add(TEXT("RealScale"), true);
		Thermal.NumericParameters.Add(TEXT("TerrainScale"), 1.0);
		Thermal.NumericParameters.Add(TEXT("Verticality"), 1.0);
		Connect(Recipe, *Last, TEXT("Out"), Thermal, TEXT("Terrain"));

		FGaeaTerrainNode& Recurve = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::Recurve, Settings.Seed, Ordinal++);
		Recurve.NumericParameters.Add(TEXT("Power"), Settings.Style == TEXT("Alpine") ? 0.24 : 0.14);
		Recurve.NumericParameters.Add(TEXT("Scale"), 0.72);
		Recurve.IntegerParameters.Add(TEXT("Iterations"), 1);
		Recurve.NameParameters.Add(TEXT("Style"), TEXT("Inward"));
		Connect(Recipe, Thermal, TEXT("Out"), Recurve, TEXT("Terrain"));
		Last = &Recurve;

		if (!Settings.bReduceDetails)
		{
			FGaeaTerrainNode& GroundTexture = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::GroundTexture, Settings.Seed, Ordinal++);
			GroundTexture.NumericParameters.Add(TEXT("Strength"), Settings.Style == TEXT("Old") ? 0.18 : 0.32);
			GroundTexture.NumericParameters.Add(TEXT("Scale"), Settings.Style == TEXT("Alpine") ? 0.72 : 0.95);
			GroundTexture.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 701);
			Connect(Recipe, Recurve, TEXT("Out"), GroundTexture, TEXT("Terrain"));
			Last = &GroundTexture;
		}

		FGaeaTerrainNode& Sharpen = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::Sharpen, Settings.Seed, Ordinal++);
		Sharpen.NameParameters.Add(TEXT("Method"), TEXT("Frequency"));
		Sharpen.NumericParameters.Add(TEXT("Amount"), Settings.Style == TEXT("Old") ? 0.18 : (Settings.Style == TEXT("Alpine") ? 0.62 : 0.42));
		Connect(Recipe, *Last, TEXT("Out"), Sharpen, TEXT("Terrain"));
		Recipe.OutputNode = Sharpen.Id;

		FGaeaTerrainEvaluationContext InnerContext = OuterContext;
		InnerContext.SourceDataset = InOutResult.Dataset;
		InnerContext.HeightScale = InOutResult.HeightScale;
		const FGaeaTerrainEvaluationResult CompositeResult = FGaeaTerrainEvaluator::Evaluate(Recipe, InnerContext);
		if (!CompositeResult.bSuccess)
		{
			Error = FString::Printf(TEXT("Mountain internal composite failed: %s"), *CompositeResult.Error);
			return false;
		}

		InOutResult.Dataset = CompositeResult.Dataset;
		InOutResult.HeightScale = CompositeResult.HeightScale;
		return NormalizeAndRefreshMountainSemantics(
			InOutResult.Dataset,
			RawHeight,
			RawMass,
			Settings.Height,
			InOutResult.HeightScale,
			OuterContext.PhysicalMetrics,
			Error);
	}

	bool EvaluateMountain(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		FGaeaMountainLandformSettings Settings;
		Settings.Resolution = 513;
		Settings.WorldSize = 100000.0f;
		Settings.HeightScale = Context.PhysicalMetrics.HasElevationScale()
			? static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0)
			: FMath::Max(Context.HeightScale, 300000.0f);
		Settings.Scale = static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0));
		Settings.Height = static_cast<float>(Node.GetNumber(TEXT("Height"), 0.92));
		Settings.Style = Node.GetName(TEXT("Style"), TEXT("Basic"));
		Settings.Bulk = Node.GetName(TEXT("Bulk"), TEXT("Medium"));
		Settings.bReduceDetails = Node.GetBool(TEXT("ReduceDetails"), false);
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		Settings.OffsetX = static_cast<float>(Node.GetNumber(TEXT("X"), 0.0));
		Settings.OffsetY = static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0));

		FGaeaMountainLandformResult Result;
		if (!FGaeaTerrainLandformOps::BuildMountain(Settings, Context.PhysicalMetrics, Result, &Error)) return false;
		if (!RunMountainComposite(Settings, Context, Result, Error)) return false;

		auto PublishScalar = [&Out, &Result](FName OutputName, FName FieldName)
		{
			if (const FGaeaScalarField* Field = Result.Dataset.FindScalarField(FieldName))
			{
				Out.Outputs.Add(OutputName, FGaeaTerrainValue::MakeScalarField(*Field));
			}
		};

		PublishScalar(TEXT("Mass"), GaeaTerrainFieldNames::MountainMass);
		PublishScalar(TEXT("Uplift"), GaeaTerrainFieldNames::Uplift);
		PublishScalar(TEXT("Ridges"), GaeaTerrainFieldNames::RidgeNetwork);
		PublishScalar(TEXT("DrainageReadiness"), GaeaTerrainFieldNames::DrainageReadiness);
		PublishScalar(TEXT("ErosionEligibility"), GaeaTerrainFieldNames::ErosionEligibility);
		PublishScalar(TEXT("RockExposure"), GaeaTerrainFieldNames::RockExposure);
		PublishScalar(TEXT("CryosphereEligibility"), GaeaTerrainFieldNames::CryosphereEligibility);

		FGaeaTerrainValue Terrain = FGaeaTerrainValue::MakeTerrain(MoveTemp(Result.Dataset), Result.HeightScale);
		if (!Terrain.IsValid())
		{
			Error = TEXT("Mountain produced an invalid terrain output.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Terrain));
		Error.Reset();
		return true;
	}
}

void RegisterGaeaTerrainLandformNodes()
{
	EnsureMountainCompositeNodeEvaluators();

	FGaeaTerrainNodeDescriptor D;
	D.Type = GaeaTerrainNodeTypes::Mountain;
	D.DisplayName = TEXT("Mountain");
	D.Category = TEXT("Terrain");
	D.Description = TEXT("Builds a detailed Gaea-style mountain as an internal EONFORM composite: distorted Voronoi mass, shaping, warping, rock structure, multi-scale erosion, thermal weathering and crest recovery.");
	D.Outputs.Add(TerrainOut());
	D.Outputs.Add(ScalarOut(TEXT("Mass"), TEXT("Mass")));
	D.Outputs.Add(ScalarOut(TEXT("Uplift"), TEXT("Uplift")));
	D.Outputs.Add(ScalarOut(TEXT("Ridges"), TEXT("Ridges")));
	D.Outputs.Add(ScalarOut(TEXT("DrainageReadiness"), TEXT("Drainage Readiness")));
	D.Outputs.Add(ScalarOut(TEXT("ErosionEligibility"), TEXT("Erosion Eligibility")));
	D.Outputs.Add(ScalarOut(TEXT("RockExposure"), TEXT("Rock Exposure")));
	D.Outputs.Add(ScalarOut(TEXT("CryosphereEligibility"), TEXT("Cryosphere Eligibility")));

	// Public controls intentionally follow Gaea's documented Mountain contract.
	D.Parameters.Add(Number(TEXT("Scale"), TEXT("Scale"), 1.0, 0.1, 2.0, TEXT("Mountain")));
	D.Parameters.Add(Number(TEXT("Height"), TEXT("Height"), 0.92, 0.0, 1.0, TEXT("Mountain")));
	D.Parameters.Add(Name(TEXT("Style"), TEXT("Style"), TEXT("Basic"), { TEXT("Basic"), TEXT("Eroded"), TEXT("Old"), TEXT("Alpine"), TEXT("Strata") }, TEXT("Mountain")));
	D.Parameters.Add(Name(TEXT("Bulk"), TEXT("Bulk"), TEXT("Medium"), { TEXT("Low"), TEXT("Medium"), TEXT("High") }, TEXT("Mountain")));
	D.Parameters.Add(Boolean(TEXT("ReduceDetails"), TEXT("Reduce Details"), false, TEXT("Mountain")));
	D.Parameters.Add(Integer(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Mountain")));
	D.Parameters.Add(Number(TEXT("X"), TEXT("X"), 0.0, -1.5, 1.5, TEXT("Position")));
	D.Parameters.Add(Number(TEXT("Y"), TEXT("Y"), 0.0, -1.5, 1.5, TEXT("Position")));

	FGaeaTerrainNodeDescriptorRegistry::Register(D);
	FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateMountain);
}
