#include "GaeaTerrainLandformNodes.h"

#include "GaeaModifySpatialNodes.h"
#include "GaeaShaperNode.h"
#include "GaeaSimulateEvolutionNodes.h"
#include "GaeaSurfaceNodes.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainLandformOps.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

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
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Erosion2)) RegisterGaeaSimulateEvolutionNodes();
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::RockNoise)) RegisterGaeaSurfaceNodes();
	}

	bool RefreshMountainSemantics(
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
		for (int32 Y = 0; Y < FinalHeight.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < FinalHeight.Domain.Dimensions.X; ++X)
			{
				const float Raw = RawHeight.AtInterior(X, Y);
				const float Mask = Smooth01(RawMass.AtInterior(X, Y));
				const float RelativeHeight = RequestedHeight > UE_SMALL_NUMBER
					? FMath::Clamp(Raw / RequestedHeight, 0.0f, 1.0f)
					: 0.0f;

				// Preserve the authored massif silhouette while allowing high-resolution
				// erosion to cut substantially into its middle/lower slopes. Processing
				// may add only small positive relief, which prevents the old needle failure.
				const float Processed = FMath::Clamp(
					ProcessedHeight->AtInterior(X, Y),
					FMath::Max(0.0f, Raw - 0.20f),
					FMath::Min(RequestedHeight, Raw + 0.025f));
				const float ProcessWeight = Mask * FMath::Lerp(0.74f, 0.38f, RelativeHeight);
				FinalHeight.AtInterior(X, Y) = FMath::Clamp(
					FMath::Lerp(Raw, Processed, ProcessWeight),
					0.0f,
					RequestedHeight);
			}
		}

		FinalHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;
		if (!Dataset.SetScalarField(MoveTemp(FinalHeight)))
		{
			Error = TEXT("Mountain composite could not publish its final Height.");
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
				const float Ridge = M * FMath::Clamp(V * 0.58f + S * 0.42f, 0.0f, 1.0f);

				Mass.AtInterior(X, Y) = M;
				Uplift.AtInterior(X, Y) = H * M;
				Ridges.AtInterior(X, Y) = Ridge;
				Drainage.AtInterior(X, Y) = M * FMath::Clamp(S * 0.58f + C * 0.42f, 0.0f, 1.0f);
				Erosion.AtInterior(X, Y) = M * FMath::Clamp(S * 0.68f + H * 0.32f, 0.0f, 1.0f);
				Rock.AtInterior(X, Y) = M * FMath::Clamp(S * 0.44f + V * 0.30f + H * 0.26f, 0.0f, 1.0f);
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
		Recipe.Nodes.Reserve(9);
		Recipe.Connections.Reserve(8);
		uint32 Ordinal = 1;

		FGaeaTerrainNode& Source = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::SourceDataset, Settings.Seed, Ordinal++);

		FGaeaTerrainNode& Shaper = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::Shaper, Settings.Seed, Ordinal++);
		Shaper.NumericParameters.Add(TEXT("Shape"), Settings.Style == TEXT("Old") ? 0.035 : 0.075);
		Shaper.NumericParameters.Add(TEXT("LocalEffect"), 0.18);
		Shaper.NumericParameters.Add(TEXT("LocalArea"), 0.52);
		Shaper.BoolParameters.Add(TEXT("MaintainFineDetails"), true);
		Shaper.NumericParameters.Add(TEXT("DetailSize"), Settings.bReduceDetails ? 0.52 : 0.30);
		Connect(Recipe, Source, TEXT("Terrain"), Shaper, TEXT("Terrain"));

		FGaeaTerrainNode& Warp = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::Warp, Settings.Seed, Ordinal++);
		Warp.NumericParameters.Add(TEXT("Size"), Settings.Style == TEXT("Alpine") ? 0.48 : 0.58);
		Warp.NumericParameters.Add(TEXT("Strength"), Settings.Style == TEXT("Old") ? 0.018 : (Settings.Style == TEXT("Alpine") ? 0.042 : 0.032));
		Warp.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 101);
		Connect(Recipe, Shaper, TEXT("Out"), Warp, TEXT("Input"));

		// First Erosion2 pass establishes the large drainage hierarchy and major gullies.
		FGaeaTerrainNode& MacroErosion = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::Erosion2, Settings.Seed, Ordinal++);
		MacroErosion.IntegerParameters.Add(TEXT("Duration"), Settings.bReduceDetails ? 8 : (Settings.Style == TEXT("Eroded") ? 26 : 18));
		MacroErosion.NumericParameters.Add(TEXT("Downcutting"), Settings.Style == TEXT("Alpine") ? 0.68 : 0.52);
		MacroErosion.NumericParameters.Add(TEXT("ErosionScale"), Settings.Style == TEXT("Alpine") ? 1.35 : 1.65);
		MacroErosion.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 307);
		MacroErosion.NumericParameters.Add(TEXT("SuspendedLoad"), 0.46);
		MacroErosion.NumericParameters.Add(TEXT("BedLoad"), 0.42);
		MacroErosion.NumericParameters.Add(TEXT("CoarseSediments"), Settings.Style == TEXT("Old") ? 0.48 : 0.30);
		MacroErosion.NumericParameters.Add(TEXT("DepositionBoost"), Settings.Style == TEXT("Old") ? 0.34 : 0.16);
		MacroErosion.NumericParameters.Add(TEXT("Shape"), Settings.Style == TEXT("Eroded") ? 0.58 : 0.42);
		MacroErosion.NumericParameters.Add(TEXT("ShapeSharpness"), Settings.Style == TEXT("Alpine") ? 0.62 : 0.46);
		MacroErosion.NumericParameters.Add(TEXT("ShapeDetailScale"), 1.0);
		MacroErosion.BoolParameters.Add(TEXT("EnableOrographic"), Settings.Style == TEXT("Alpine"));
		MacroErosion.NumericParameters.Add(TEXT("Direction"), 25.0);
		MacroErosion.NumericParameters.Add(TEXT("DirectionalPrecipitation"), 0.32);
		MacroErosion.NumericParameters.Add(TEXT("RainShadow"), 0.18);
		Connect(Recipe, Warp, TEXT("Out"), MacroErosion, TEXT("Terrain"));

		FGaeaTerrainNode* Last = &MacroErosion;
		if (!Settings.bReduceDetails)
		{
			// RockNoise introduces restrained lithologic breakup before the second
			// erosion scale. Its amplitude is intentionally low; erosion, not noise,
			// determines the visible drainage pattern.
			FGaeaTerrainNode& Rock = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::RockNoise, Settings.Seed, Ordinal++);
			Rock.NumericParameters.Add(TEXT("Strength"), Settings.Style == TEXT("Old") ? 0.08 : (Settings.Style == TEXT("Alpine") ? 0.15 : 0.11));
			Rock.NumericParameters.Add(TEXT("Scale"), Settings.Style == TEXT("Alpine") ? 0.72 : 0.88);
			Rock.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 401);
			Connect(Recipe, MacroErosion, TEXT("Out"), Rock, TEXT("Terrain"));

			// Fine Erosion2 pass turns the new rock variation into tributaries, rills,
			// and smaller-scale incision rather than leaving it as procedural noise.
			FGaeaTerrainNode& FineErosion = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::Erosion2, Settings.Seed, Ordinal++);
			FineErosion.IntegerParameters.Add(TEXT("Duration"), Settings.Style == TEXT("Eroded") ? 12 : 8);
			FineErosion.NumericParameters.Add(TEXT("Downcutting"), Settings.Style == TEXT("Alpine") ? 0.58 : 0.42);
			FineErosion.NumericParameters.Add(TEXT("ErosionScale"), Settings.Style == TEXT("Alpine") ? 0.42 : 0.55);
			FineErosion.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 503);
			FineErosion.NumericParameters.Add(TEXT("SuspendedLoad"), 0.34);
			FineErosion.NumericParameters.Add(TEXT("BedLoad"), 0.28);
			FineErosion.NumericParameters.Add(TEXT("CoarseSediments"), 0.18);
			FineErosion.NumericParameters.Add(TEXT("DepositionBoost"), 0.08);
			FineErosion.NumericParameters.Add(TEXT("Shape"), 0.20);
			FineErosion.NumericParameters.Add(TEXT("ShapeSharpness"), 0.38);
			FineErosion.NumericParameters.Add(TEXT("ShapeDetailScale"), 0.72);
			Connect(Recipe, Rock, TEXT("Out"), FineErosion, TEXT("Terrain"));
			Last = &FineErosion;
		}

		// Thermal2 works in physical metres, so talus and cliff breakdown scale with
		// the actual world/sample spacing instead of with arbitrary texture pixels.
		FGaeaTerrainNode& Thermal = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::Thermal2, Settings.Seed, Ordinal++);
		Thermal.IntegerParameters.Add(TEXT("Duration"), Settings.bReduceDetails ? 5 : (Settings.Style == TEXT("Old") ? 14 : 9));
		Thermal.NumericParameters.Add(TEXT("Strength"), Settings.Style == TEXT("Old") ? 0.34 : 0.23);
		Thermal.NumericParameters.Add(TEXT("Anisotropy"), Settings.Style == TEXT("Alpine") ? 0.12 : 0.04);
		Thermal.NumericParameters.Add(TEXT("Angle"), Settings.Style == TEXT("Alpine") ? 39.0 : 35.0);
		Thermal.NumericParameters.Add(TEXT("SedimentRemoval"), Settings.Style == TEXT("Alpine") ? 0.12 : 0.04);
		Thermal.NumericParameters.Add(TEXT("FeatureScale"), Settings.Style == TEXT("Alpine") ? 24.0 : 38.0);
		Connect(Recipe, *Last, TEXT("Out"), Thermal, TEXT("Terrain"));
		Last = &Thermal;

		if (!Settings.bReduceDetails)
		{
			// Final tiny surface octave. This is intentionally subtle and occurs after
			// erosion/talus; it supplies weathered high-frequency relief without
			// changing the mountain's silhouette or manufacturing spikes.
			FGaeaTerrainNode& Ground = AddCompositeNode(Recipe, GaeaTerrainNodeTypes::GroundTexture, Settings.Seed, Ordinal++);
			Ground.NumericParameters.Add(TEXT("Strength"), Settings.Style == TEXT("Old") ? 0.07 : (Settings.Style == TEXT("Alpine") ? 0.13 : 0.10));
			Ground.NumericParameters.Add(TEXT("Scale"), Settings.Style == TEXT("Alpine") ? 0.70 : 0.86);
			Ground.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Settings.Seed) + 701);
			Connect(Recipe, Thermal, TEXT("Out"), Ground, TEXT("Terrain"));
			Last = &Ground;
		}

		Recipe.OutputNode = Last->Id;

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
		return RefreshMountainSemantics(
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
		Settings.bReduceDetails = Node.GetBool(TEXT("ReduceDetails"), false);

		// Mountain must be generated at the requested working resolution. Merely
		// resampling a 513 field into a 2017 mesh cannot create erosion or rock data.
		const int32 NativeResolution = Settings.bReduceDetails ? 513 : 1009;
		const int32 RequestedResolution = Context.TargetResolution.X > 0
			? FMath::Max(Context.TargetResolution.X, Context.TargetResolution.Y)
			: NativeResolution;
		const int32 WorkingCeiling = Settings.bReduceDetails ? 1009 : 2017;
		Settings.Resolution = FMath::Clamp(RequestedResolution, 257, WorkingCeiling);

		Settings.WorldSize = 100000.0f;
		Settings.HeightScale = Context.PhysicalMetrics.HasElevationScale()
			? static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0)
			: FMath::Max(Context.HeightScale, 300000.0f);
		Settings.Scale = static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0));
		Settings.Height = static_cast<float>(Node.GetNumber(TEXT("Height"), 0.92));
		Settings.Style = Node.GetName(TEXT("Style"), TEXT("Basic"));
		Settings.Bulk = Node.GetName(TEXT("Bulk"), TEXT("Medium"));
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
	D.Description = TEXT("Builds a high-resolution Gaea-style mountain from a hierarchical summit/ridge primitive, multi-scale hydraulic erosion, lithologic breakup, physical talus weathering, and fine surface detail.");
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
