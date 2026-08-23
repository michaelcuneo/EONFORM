#include "GaeaTerrainLandformNodes.h"

#include "GaeaReferenceFidelityMountainNodes.h"
#include "GaeaReferenceFidelityNodes.h"
#include "GaeaReferenceFidelityProcessNodes.h"
#include "GaeaShaperNode.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
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

	FGuid CompositeId(int32 Seed, uint32 Ordinal)
	{
		return FGuid(0x4D544E00u + Ordinal, 0x454F4E46u, static_cast<uint32>(Seed), 0x434F4D50u);
	}

	FGaeaTerrainNode& AddNode(FGaeaTerrainRecipe& Recipe, FName Type, int32 Seed, uint32 Ordinal)
	{
		FGaeaTerrainNode Node;
		Node.Id = CompositeId(Seed, Ordinal);
		Node.Type = Type;
		return Recipe.Nodes.Add_GetRef(MoveTemp(Node));
	}

	void Link(FGaeaTerrainRecipe& Recipe, const FGaeaTerrainNode& From, FName Output, const FGaeaTerrainNode& To, FName Input)
	{
		FGaeaTerrainConnection Connection;
		Connection.FromNode = From.Id;
		Connection.FromOutput = Output;
		Connection.ToNode = To.Id;
		Connection.ToInput = Input;
		Recipe.Connections.Add(Connection);
	}

	void EnsureAuditedMountainNodes()
	{
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::RadialGradient)
			|| !FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Voronoi)
			|| !FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Combine))
		{
			RegisterGaeaReferenceFidelityNodes();
		}
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::SlopeWarp)
			|| !FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Thermal2))
		{
			RegisterGaeaReferenceFidelityMountainNodes();
		}
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Erosion2))
		{
			RegisterGaeaReferenceFidelityProcessNodes();
		}
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Shaper))
		{
			RegisterGaeaShaperNode();
		}
	}

	double ResolveReferenceWorldMeters(const FGaeaTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasWorldDimensions())
		{
			return FMath::Max(
				FMath::Min(
					FMath::Abs(Context.PhysicalMetrics.WorldWidthMeters),
					FMath::Abs(Context.PhysicalMetrics.WorldDepthMeters)),
				1.0);
		}
		return 10000.0;
	}

	bool NormalizeMountainHeight(
		FGaeaTerrainDataset& Dataset,
		float RequestedHeight,
		FString& Error)
	{
		const FGaeaScalarField* Source = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Source || !Source->IsValid())
		{
			Error = TEXT("Mountain composite produced no valid Height field.");
			return false;
		}

		float MaxHeight = TNumericLimits<float>::Lowest();
		for (const float Value : Source->Values)
		{
			MaxHeight = FMath::Max(MaxHeight, Value);
		}
		if (MaxHeight <= UE_SMALL_NUMBER)
		{
			Error = TEXT("Mountain composite produced no positive relief.");
			return false;
		}

		// Keep the public Height control authoritative while leaving a small safety
		// margin below normalized saturation when Height is authored at 1.0.
		const float TargetHeight = FMath::Clamp(RequestedHeight, 0.0f, 0.985f);
		const float Scale = TargetHeight / MaxHeight;
		FGaeaScalarField Height = *Source;
		for (float& Value : Height.Values)
		{
			Value = FMath::Clamp(Value * Scale, 0.0f, TargetHeight);
		}
		Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Mountain could not publish normalized Height.");
			return false;
		}
		return true;
	}

	bool RebuildSemantics(
		FGaeaTerrainDataset& Dataset,
		float RequestedHeight,
		float HeightScale,
		const FGaeaTerrainPhysicalMetrics& Metrics,
		FString& Error)
	{
		if (!FGaeaTerrainDerivedData::EnsureContext(Dataset, HeightScale, Metrics, &Error)) return false;
		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* Slope = Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		const FGaeaScalarField* Concavity = Dataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
		const FGaeaScalarField* Convexity = Dataset.FindScalarField(GaeaTerrainFieldNames::Convexity);
		if (!Height || !Slope || !Concavity || !Convexity)
		{
			Error = TEXT("Mountain could not rebuild final semantic fields.");
			return false;
		}

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
		const float Denominator = FMath::Max(FMath::Min(RequestedHeight, 0.985f), UE_SMALL_NUMBER);

		for (int32 Y = 0; Y < Height->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
			{
				const float Height01 = FMath::Clamp(Height->AtInterior(X, Y) / Denominator, 0.0f, 1.0f);
				const float M = Smooth01(Height01);
				const float S = FMath::Clamp(Slope->AtInterior(X, Y) / 70.0f, 0.0f, 1.0f);
				const float C = FMath::Clamp(Concavity->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
				const float V = FMath::Clamp(Convexity->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
				const float R = M * FMath::Clamp(V * 0.58f + S * 0.42f, 0.0f, 1.0f);

				Mass.AtInterior(X, Y) = M;
				Uplift.AtInterior(X, Y) = Height01 * M;
				Ridge.AtInterior(X, Y) = R;
				Drainage.AtInterior(X, Y) = M * FMath::Clamp(S * 0.52f + C * 0.48f, 0.0f, 1.0f);
				Erosion.AtInterior(X, Y) = M * FMath::Clamp(S * 0.72f + Height01 * 0.28f, 0.0f, 1.0f);
				Rock.AtInterior(X, Y) = M * FMath::Clamp(S * 0.48f + V * 0.30f + Height01 * 0.22f, 0.0f, 1.0f);
				Cryosphere.AtInterior(X, Y) = M * Smooth01((Height01 - 0.58f) / 0.32f) * FMath::Lerp(0.72f, 1.0f, R);
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
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		EnsureAuditedMountainNodes();

		const bool bReduceDetails = Node.GetBool(TEXT("ReduceDetails"), false);
		const float MountainScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.1f, 2.0f);
		const float RequestedHeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 0.92)), 0.0f, 1.0f);
		const FName Style = Node.GetName(TEXT("Style"), TEXT("Basic"));
		const FName Bulk = Node.GetName(TEXT("Bulk"), TEXT("Medium"));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float OffsetX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("X"), 0.0)), -1.5f, 1.5f);
		const float OffsetY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0)), -1.5f, 1.5f);
		const double ReferenceWorldMeters = ResolveReferenceWorldMeters(Context);

		const float BulkFactor = Bulk == TEXT("Low") ? 0.82f : Bulk == TEXT("High") ? 1.18f : 1.0f;
		const float SupportScale = FMath::Clamp(MountainScale * BulkFactor * 1.06f, 0.16f, 2.0f);
		const float PeakScale = FMath::Clamp(0.24f / FMath::Max(MountainScale * BulkFactor, 0.15f), 0.08f, 0.72f);
		const float RidgeScale = FMath::Clamp(PeakScale * 0.82f, 0.06f, 0.60f);
		const double MacroErosionMeters = FMath::Clamp(ReferenceWorldMeters * (Style == TEXT("Alpine") ? 0.026 : Style == TEXT("Eroded") ? 0.038 : 0.034), 450.0, 6000.0);
		const double FineErosionMeters = FMath::Clamp(MacroErosionMeters * (Style == TEXT("Alpine") ? 0.28 : 0.36), 140.0, 1800.0);
		const double ThermalFeatureMeters = FMath::Clamp(ReferenceWorldMeters * (Style == TEXT("Old") ? 0.007 : Style == TEXT("Alpine") ? 0.0035 : 0.005), 35.0, 650.0);

		FGaeaTerrainRecipe Recipe;
		Recipe.Nodes.Reserve(11);
		Recipe.Connections.Reserve(13);
		uint32 Ordinal = 1;

		FGaeaTerrainNode& Support = AddNode(Recipe, GaeaTerrainNodeTypes::RadialGradient, Seed, Ordinal++);
		Support.NumericParameters.Add(TEXT("Scale"), SupportScale);
		Support.NumericParameters.Add(TEXT("Height"), 1.0);
		Support.NumericParameters.Add(TEXT("X"), OffsetX);
		Support.NumericParameters.Add(TEXT("Y"), OffsetY);

		FGaeaTerrainNode& Peaks = AddNode(Recipe, GaeaTerrainNodeTypes::Voronoi, Seed, Ordinal++);
		Peaks.NumericParameters.Add(TEXT("Scale"), PeakScale);
		Peaks.NumericParameters.Add(TEXT("Jitter"), Style == TEXT("Alpine") ? 1.18 : 0.92);
		Peaks.NameParameters.Add(TEXT("Form"), Style == TEXT("Old") ? TEXT("A") : TEXT("P"));
		Peaks.NumericParameters.Add(TEXT("Gain"), Style == TEXT("Alpine") ? 1.15 : 0.92);
		Peaks.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Seed) + 17);
		Peaks.NameParameters.Add(TEXT("WarpType"), TEXT("Complex"));
		Peaks.NumericParameters.Add(TEXT("WarpFrequency"), Style == TEXT("Alpine") ? 0.78 : 0.58);
		Peaks.NumericParameters.Add(TEXT("WarpAmplitude"), Style == TEXT("Old") ? 0.18 : 0.32);
		Peaks.IntegerParameters.Add(TEXT("WarpOctaves"), 3);
		Peaks.NumericParameters.Add(TEXT("ScaleX"), Bulk == TEXT("High") ? 0.78 : 1.0);
		Peaks.NumericParameters.Add(TEXT("ScaleY"), Bulk == TEXT("Low") ? 1.12 : 0.92);
		Peaks.NumericParameters.Add(TEXT("X"), OffsetX * 0.08f);
		Peaks.NumericParameters.Add(TEXT("Y"), OffsetY * 0.08f);

		FGaeaTerrainNode& RidgeGuide = AddNode(Recipe, GaeaTerrainNodeTypes::Voronoi, Seed, Ordinal++);
		RidgeGuide.NumericParameters.Add(TEXT("Scale"), RidgeScale);
		RidgeGuide.NumericParameters.Add(TEXT("Jitter"), 1.05);
		RidgeGuide.NameParameters.Add(TEXT("Form"), TEXT("R"));
		RidgeGuide.NumericParameters.Add(TEXT("Gain"), Style == TEXT("Alpine") ? 1.32 : 1.02);
		RidgeGuide.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Seed) + 83);
		RidgeGuide.NameParameters.Add(TEXT("WarpType"), TEXT("Complex"));
		RidgeGuide.NumericParameters.Add(TEXT("WarpFrequency"), 0.72);
		RidgeGuide.NumericParameters.Add(TEXT("WarpAmplitude"), 0.26);
		RidgeGuide.IntegerParameters.Add(TEXT("WarpOctaves"), 3);

		FGaeaTerrainNode& Base = AddNode(Recipe, GaeaTerrainNodeTypes::Combine, Seed, Ordinal++);
		Base.NameParameters.Add(TEXT("Mode"), TEXT("Multiply"));
		Base.NumericParameters.Add(TEXT("Ratio"), 1.0);
		Base.NameParameters.Add(TEXT("Output"), TEXT("Clamp"));
		Link(Recipe, Support, TEXT("Out"), Base, TEXT("Input1"));
		Link(Recipe, Peaks, TEXT("Out"), Base, TEXT("Input2"));

		FGaeaTerrainNode& Shaper = AddNode(Recipe, GaeaTerrainNodeTypes::Shaper, Seed, Ordinal++);
		Shaper.NumericParameters.Add(TEXT("Shape"), Style == TEXT("Old") ? 0.018 : Style == TEXT("Alpine") ? 0.075 : 0.050);
		Shaper.NumericParameters.Add(TEXT("LocalEffect"), Style == TEXT("Alpine") ? 0.18 : 0.10);
		Shaper.NumericParameters.Add(TEXT("LocalArea"), 0.52);
		Shaper.BoolParameters.Add(TEXT("MaintainFineDetails"), true);
		Shaper.NumericParameters.Add(TEXT("DetailSize"), bReduceDetails ? 0.55 : 0.30);
		Link(Recipe, Base, TEXT("Out"), Shaper, TEXT("Terrain"));

		FGaeaTerrainNode& SlopeWarp = AddNode(Recipe, GaeaTerrainNodeTypes::SlopeWarp, Seed, Ordinal++);
		SlopeWarp.NumericParameters.Add(TEXT("Intensity"), Style == TEXT("Alpine") ? 0.34 : Style == TEXT("Old") ? 0.17 : 0.25);
		SlopeWarp.IntegerParameters.Add(TEXT("Iterations"), bReduceDetails ? 1 : 2);
		SlopeWarp.NumericParameters.Add(TEXT("Direction"), Style == TEXT("Strata") ? 18.0 : 0.0);
		SlopeWarp.BoolParameters.Add(TEXT("Normalized"), true);
		SlopeWarp.NameParameters.Add(TEXT("Quality"), bReduceDetails ? TEXT("Medium") : TEXT("High"));
		SlopeWarp.NameParameters.Add(TEXT("Antialiasing"), bReduceDetails ? TEXT("Off") : TEXT("X 4"));
		Link(Recipe, Shaper, TEXT("Out"), SlopeWarp, TEXT("Input"));
		Link(Recipe, RidgeGuide, TEXT("Out"), SlopeWarp, TEXT("Guide"));

		FGaeaTerrainNode& Macro = AddNode(Recipe, GaeaTerrainNodeTypes::Erosion2, Seed, Ordinal++);
		Macro.IntegerParameters.Add(TEXT("Duration"), bReduceDetails ? 12 : Style == TEXT("Eroded") ? 42 : Style == TEXT("Alpine") ? 34 : 28);
		Macro.NumericParameters.Add(TEXT("Downcutting"), Style == TEXT("Alpine") ? 0.76 : Style == TEXT("Old") ? 0.42 : 0.60);
		Macro.NumericParameters.Add(TEXT("ErosionScale"), MacroErosionMeters);
		Macro.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Seed) + 307);
		Macro.NumericParameters.Add(TEXT("SuspendedLoad"), 0.58);
		Macro.NumericParameters.Add(TEXT("BedLoad"), 0.48);
		Macro.NumericParameters.Add(TEXT("CoarseSediments"), Style == TEXT("Alpine") ? 0.36 : 0.28);
		Macro.NumericParameters.Add(TEXT("DepositionBoost"), 0.18);
		Macro.NumericParameters.Add(TEXT("Shape"), Style == TEXT("Alpine") ? 0.62 : 0.48);
		Macro.NumericParameters.Add(TEXT("ShapeSharpness"), Style == TEXT("Alpine") ? 0.68 : 0.50);
		Macro.NumericParameters.Add(TEXT("ShapeDetailScale"), bReduceDetails ? 0.45 : 0.88);
		Macro.BoolParameters.Add(TEXT("EnableOrographic"), Style == TEXT("Alpine"));
		Macro.NumericParameters.Add(TEXT("Direction"), 25.0);
		Macro.NumericParameters.Add(TEXT("DirectionalPrecipitation"), 0.42);
		Macro.NumericParameters.Add(TEXT("RainShadow"), 0.12);
		Link(Recipe, SlopeWarp, TEXT("Out"), Macro, TEXT("Terrain"));

		FGaeaTerrainNode* LastProcess = &Macro;
		if (!bReduceDetails)
		{
			FGaeaTerrainNode& Fine = AddNode(Recipe, GaeaTerrainNodeTypes::Erosion2, Seed, Ordinal++);
			Fine.IntegerParameters.Add(TEXT("Duration"), Style == TEXT("Eroded") ? 24 : 16);
			Fine.NumericParameters.Add(TEXT("Downcutting"), Style == TEXT("Alpine") ? 0.62 : 0.46);
			Fine.NumericParameters.Add(TEXT("ErosionScale"), FineErosionMeters);
			Fine.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Seed) + 503);
			Fine.NumericParameters.Add(TEXT("SuspendedLoad"), 0.38);
			Fine.NumericParameters.Add(TEXT("BedLoad"), 0.30);
			Fine.NumericParameters.Add(TEXT("CoarseSediments"), 0.18);
			Fine.NumericParameters.Add(TEXT("DepositionBoost"), 0.10);
			Fine.NumericParameters.Add(TEXT("Shape"), 0.30);
			Fine.NumericParameters.Add(TEXT("ShapeSharpness"), 0.48);
			Fine.NumericParameters.Add(TEXT("ShapeDetailScale"), 0.72);
			Link(Recipe, Macro, TEXT("Out"), Fine, TEXT("Terrain"));
			LastProcess = &Fine;
		}

		FGaeaTerrainNode& Thermal = AddNode(Recipe, GaeaTerrainNodeTypes::Thermal2, Seed, Ordinal++);
		Thermal.IntegerParameters.Add(TEXT("Duration"), bReduceDetails ? 5 : Style == TEXT("Old") ? 16 : 10);
		Thermal.NumericParameters.Add(TEXT("Strength"), Style == TEXT("Old") ? 0.36 : Style == TEXT("Alpine") ? 0.20 : 0.24);
		Thermal.NumericParameters.Add(TEXT("Anisotropy"), Style == TEXT("Alpine") ? 0.10 : 0.03);
		Thermal.NumericParameters.Add(TEXT("Angle"), Style == TEXT("Alpine") ? 39.0 : 35.0);
		Thermal.NumericParameters.Add(TEXT("SedimentRemoval"), Style == TEXT("Alpine") ? 0.10 : 0.03);
		Thermal.NumericParameters.Add(TEXT("FeatureScale"), ThermalFeatureMeters);
		Link(Recipe, *LastProcess, TEXT("Out"), Thermal, TEXT("Terrain"));

		// Re-apply the authored mountain support only after all process simulation.
		// This is a geometric multiply of the processed terrain, not a blend of the
		// pristine source back over erosion, so Wear/Flow/Deposits remain truthful.
		FGaeaTerrainNode& Supported = AddNode(Recipe, GaeaTerrainNodeTypes::Combine, Seed, Ordinal++);
		Supported.NameParameters.Add(TEXT("Mode"), TEXT("Multiply"));
		Supported.NumericParameters.Add(TEXT("Ratio"), 1.0);
		Supported.NameParameters.Add(TEXT("Output"), TEXT("Clamp"));
		Link(Recipe, Thermal, TEXT("Out"), Supported, TEXT("Input1"));
		Link(Recipe, Support, TEXT("Out"), Supported, TEXT("Input2"));
		Recipe.OutputNode = Supported.Id;

		FGaeaTerrainEvaluationContext InnerContext = Context;
		const FGaeaTerrainEvaluationResult Evaluation = FGaeaTerrainEvaluator::Evaluate(Recipe, InnerContext);
		if (!Evaluation.bSuccess)
		{
			Error = FString::Printf(TEXT("Mountain audited composite failed: %s"), *Evaluation.Error);
			return false;
		}

		FGaeaTerrainDataset Dataset = Evaluation.Dataset;
		const float HeightScale = Evaluation.HeightScale;
		if (!NormalizeMountainHeight(Dataset, RequestedHeight, Error)) return false;
		if (!RebuildSemantics(Dataset, RequestedHeight, HeightScale, Context.PhysicalMetrics, Error)) return false;

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
	EnsureAuditedMountainNodes();

	FGaeaTerrainNodeDescriptor D;
	D.Type = GaeaTerrainNodeTypes::Mountain;
	D.DisplayName = TEXT("Mountain");
	D.Category = TEXT("Terrain");
	D.Description = TEXT("Process-authored mountain landform built from audited primitives, ridge warping, hydraulic incision and thermal weathering.");
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
	D.Parameters.Add(Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.1, 2.0, TEXT("Mountain")));
	D.Parameters.Add(Num(TEXT("Height"), TEXT("Height"), 0.92, 0.0, 1.0, TEXT("Mountain")));
	D.Parameters.Add(Choice(TEXT("Style"), TEXT("Style"), TEXT("Basic"), { TEXT("Basic"), TEXT("Eroded"), TEXT("Old"), TEXT("Alpine"), TEXT("Strata") }, TEXT("Mountain")));
	D.Parameters.Add(Choice(TEXT("Bulk"), TEXT("Bulk"), TEXT("Medium"), { TEXT("Low"), TEXT("Medium"), TEXT("High") }, TEXT("Mountain")));
	D.Parameters.Add(Bool(TEXT("ReduceDetails"), TEXT("Reduce Details"), false, TEXT("Mountain")));
	D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Mountain")));
	D.Parameters.Add(Num(TEXT("X"), TEXT("X"), 0.0, -1.5, 1.5, TEXT("Position")));
	D.Parameters.Add(Num(TEXT("Y"), TEXT("Y"), 0.0, -1.5, 1.5, TEXT("Position")));
	FGaeaTerrainNodeDescriptorRegistry::Register(D);
	FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateMountain);
}
