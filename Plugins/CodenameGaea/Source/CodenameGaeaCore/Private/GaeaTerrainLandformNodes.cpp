#include "GaeaTerrainLandformNodes.h"

#include "GaeaReferenceFidelityExtendedNodes.h"
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
#include "Math/RandomStream.h"

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
		P.Maximum = Max;
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
			|| !FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Thermal2)
			|| !FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Craggy))
		{
			RegisterGaeaReferenceFidelityMountainNodes();
		}
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Erosion2))
		{
			RegisterGaeaReferenceFidelityProcessNodes();
		}
		if (!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Stratify))
		{
			RegisterGaeaReferenceFidelityExtendedNodes();
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

	bool ConstrainMountainHeight(
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

		const float TargetHeight = FMath::Clamp(RequestedHeight, 0.0f, 0.985f);
		const float Scale = MaxHeight > TargetHeight && MaxHeight > UE_SMALL_NUMBER
			? TargetHeight / MaxHeight
			: 1.0f;
		FGaeaScalarField Height = *Source;
		for (float& Value : Height.Values)
		{
			Value = FMath::Clamp(Value * Scale, 0.0f, TargetHeight);
		}
		Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Mountain could not publish constrained Height.");
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
				const float RidgeMass = Smooth01(FMath::Clamp((Height01 - 0.08f) / 0.72f, 0.0f, 1.0f));
				const float S = FMath::Clamp(Slope->AtInterior(X, Y) / 70.0f, 0.0f, 1.0f);
				const float C = FMath::Clamp(Concavity->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
				const float V = FMath::Clamp(Convexity->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
				const float RidgeShape = FMath::Clamp(0.28f + V * 0.36f + S * 0.24f + Height01 * 0.12f, 0.0f, 1.0f);
				const float R = FMath::Clamp(RidgeMass * RidgeShape, 0.0f, 1.0f);

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
		const float MountainScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.01f, 2.0f);
		const float RequestedHeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 0.92)), 0.0f, 1.0f);
		const FName Style = Node.GetName(TEXT("Style"), TEXT("Basic"));
		const FName Bulk = Node.GetName(TEXT("Bulk"), TEXT("Medium"));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float OffsetX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("X"), 0.0)), -1.5f, 1.5f);
		const float OffsetY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0)), -1.5f, 1.5f);
		const double ReferenceWorldMeters = ResolveReferenceWorldMeters(Context);

		const bool bEroded = Style == TEXT("Eroded");
		const bool bOld = Style == TEXT("Old");
		const bool bAlpine = Style == TEXT("Alpine");
		const bool bStrata = Style == TEXT("Strata");
		const float BulkFactor = Bulk == TEXT("Low") ? 0.82f : Bulk == TEXT("High") ? 1.18f : 1.0f;

		// Gaea's Scale behaves like the perceptual footprint of the complete
		// mountain. Preserve approximately the same amount of internal structure
		// inside that footprint by inversely scaling the structural Voronoi fields.
		const float FootprintScale = FMath::Clamp(MountainScale * BulkFactor * (bAlpine ? 0.86f : 0.90f), 0.01f, 3.6f);
		const float StructureScale = FMath::Clamp((bAlpine ? 0.86f : 0.72f) / FMath::Max(MountainScale * BulkFactor, 0.01f), 0.04f, 16.0f);
		const float RidgeScale = FMath::Clamp(StructureScale * (bAlpine ? 1.34f : 1.18f), 0.04f, 16.0f);
		const double MacroErosionMeters = FMath::Clamp(ReferenceWorldMeters * (bOld ? 0.050 : bEroded ? 0.036 : bAlpine ? 0.024 : 0.030), 250.0, 7000.0);
		const double FineErosionMeters = FMath::Clamp(MacroErosionMeters * (bOld ? 0.30 : bAlpine ? 0.26 : 0.34), 80.0, 2000.0);
		const double ThermalFeatureMeters = FMath::Clamp(ReferenceWorldMeters * (bOld ? 0.009 : bAlpine ? 0.0035 : 0.005), 30.0, 800.0);

		FGaeaTerrainRecipe Recipe;
		Recipe.Nodes.Reserve(32);
		Recipe.Connections.Reserve(40);
		uint32 Ordinal = 1;
		FRandomStream LayoutRandom(Seed ^ 0x4D544E31);

		// ---- Massif envelope ---------------------------------------------------
		// A single radial field produces a circular dome. Build a deterministic
		// cluster of overlapping low-frequency support lobes instead. Max-combining
		// them creates shoulders and asymmetric sub-masses without inventing a new
		// terrain primitive; the public Mountain node remains an orchestration macro.
		auto AddSupportLobe = [&](float ScaleMultiplier, float HeightMultiplier, float DistanceMultiplier) -> FGaeaTerrainNode&
		{
			const float Angle = LayoutRandom.FRandRange(0.0f, 2.0f * PI);
			const float Distance = MountainScale * LayoutRandom.FRandRange(DistanceMultiplier * 0.72f, DistanceMultiplier * 1.18f);
			FGaeaTerrainNode& Lobe = AddNode(Recipe, GaeaTerrainNodeTypes::RadialGradient, Seed, Ordinal++);
			Lobe.NumericParameters.Add(TEXT("Scale"), FMath::Clamp(FootprintScale * ScaleMultiplier, 0.001f, 4.0f));
			Lobe.NumericParameters.Add(TEXT("Height"), HeightMultiplier);
			Lobe.NumericParameters.Add(TEXT("X"), OffsetX + FMath::Cos(Angle) * Distance);
			Lobe.NumericParameters.Add(TEXT("Y"), OffsetY + FMath::Sin(Angle) * Distance);
			return Lobe;
		};

		FGaeaTerrainNode& SupportCore = AddNode(Recipe, GaeaTerrainNodeTypes::RadialGradient, Seed, Ordinal++);
		SupportCore.NumericParameters.Add(TEXT("Scale"), FMath::Clamp(FootprintScale * (bAlpine ? 0.70f : 0.76f), 0.001f, 4.0f));
		SupportCore.NumericParameters.Add(TEXT("Height"), 1.0);
		SupportCore.NumericParameters.Add(TEXT("X"), OffsetX);
		SupportCore.NumericParameters.Add(TEXT("Y"), OffsetY);

		FGaeaTerrainNode& SupportA = AddSupportLobe(bAlpine ? 0.62f : 0.66f, 0.90f, 0.14f);
		FGaeaTerrainNode& SupportB = AddSupportLobe(bAlpine ? 0.52f : 0.57f, 0.80f, 0.20f);
		FGaeaTerrainNode& SupportC = AddSupportLobe(bAlpine ? 0.44f : 0.49f, 0.70f, 0.25f);

		auto MaxCombine = [&](FGaeaTerrainNode& A, FGaeaTerrainNode& B) -> FGaeaTerrainNode&
		{
			FGaeaTerrainNode& Combine = AddNode(Recipe, GaeaTerrainNodeTypes::Combine, Seed, Ordinal++);
			Combine.NameParameters.Add(TEXT("Mode"), TEXT("Max"));
			Combine.NumericParameters.Add(TEXT("Ratio"), 1.0);
			Combine.NameParameters.Add(TEXT("Output"), TEXT("Clamp"));
			Link(Recipe, A, TEXT("Out"), Combine, TEXT("Input1"));
			Link(Recipe, B, TEXT("Out"), Combine, TEXT("Input2"));
			return Combine;
		};

		FGaeaTerrainNode& SupportAB = MaxCombine(SupportCore, SupportA);
		FGaeaTerrainNode& SupportABC = MaxCombine(SupportAB, SupportB);
		FGaeaTerrainNode& Support = MaxCombine(SupportABC, SupportC);

		// ---- Hierarchical structural fields ------------------------------------
		// Gaea describes Mountain as modulated Voronoi plus distortion. One P-form
		// field is not enough: use a broad M-form mass field, a P/A peak field and
		// two ridge scales so the result has primary mass, subsidiary summits and
		// ridge hierarchy before erosion ever runs.
		const float AxisBias = LayoutRandom.FRandRange(0.84f, 1.16f);
		FGaeaTerrainNode& MassCells = AddNode(Recipe, GaeaTerrainNodeTypes::Voronoi, Seed, Ordinal++);
		MassCells.NumericParameters.Add(TEXT("Scale"), FMath::Clamp(StructureScale * 0.72f, 0.02f, 16.0f));
		MassCells.NumericParameters.Add(TEXT("Jitter"), bAlpine ? 1.18 : 1.08);
		MassCells.NameParameters.Add(TEXT("Form"), bAlpine ? TEXT("A") : TEXT("M"));
		MassCells.NumericParameters.Add(TEXT("Gain"), bAlpine ? 1.12 : 0.96);
		MassCells.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Seed) + 17);
		MassCells.NameParameters.Add(TEXT("WarpType"), TEXT("Complex"));
		MassCells.NumericParameters.Add(TEXT("WarpFrequency"), bAlpine ? 0.72 : 0.56);
		MassCells.NumericParameters.Add(TEXT("WarpAmplitude"), bAlpine ? 0.50 : 0.40);
		MassCells.IntegerParameters.Add(TEXT("WarpOctaves"), bReduceDetails ? 2 : 4);
		MassCells.NumericParameters.Add(TEXT("ScaleX"), AxisBias * (Bulk == TEXT("High") ? 0.88f : 1.0f));
		MassCells.NumericParameters.Add(TEXT("ScaleY"), (2.0f - AxisBias) * (Bulk == TEXT("Low") ? 1.10f : 1.0f));
		MassCells.NumericParameters.Add(TEXT("X"), -OffsetX * 0.18f);
		MassCells.NumericParameters.Add(TEXT("Y"), -OffsetY * 0.18f);

		FGaeaTerrainNode& Peaks = AddNode(Recipe, GaeaTerrainNodeTypes::Voronoi, Seed, Ordinal++);
		Peaks.NumericParameters.Add(TEXT("Scale"), FMath::Clamp(StructureScale * (bAlpine ? 1.12f : 1.0f), 0.02f, 16.0f));
		Peaks.NumericParameters.Add(TEXT("Jitter"), bAlpine ? 1.24 : 1.12);
		Peaks.NameParameters.Add(TEXT("Form"), bAlpine ? TEXT("A") : TEXT("P"));
		Peaks.NumericParameters.Add(TEXT("Gain"), bAlpine ? 1.24 : 1.08);
		Peaks.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Seed) + 47);
		Peaks.NameParameters.Add(TEXT("WarpType"), TEXT("Complex"));
		Peaks.NumericParameters.Add(TEXT("WarpFrequency"), bAlpine ? 0.86 : 0.68);
		Peaks.NumericParameters.Add(TEXT("WarpAmplitude"), bAlpine ? 0.58 : 0.48);
		Peaks.IntegerParameters.Add(TEXT("WarpOctaves"), bReduceDetails ? 2 : 4);
		Peaks.NumericParameters.Add(TEXT("ScaleX"), (2.0f - AxisBias) * 0.96f);
		Peaks.NumericParameters.Add(TEXT("ScaleY"), AxisBias * 1.04f);
		Peaks.NumericParameters.Add(TEXT("X"), -OffsetX * 0.13f);
		Peaks.NumericParameters.Add(TEXT("Y"), -OffsetY * 0.13f);

		FGaeaTerrainNode& StructuralMass = AddNode(Recipe, GaeaTerrainNodeTypes::Combine, Seed, Ordinal++);
		StructuralMass.NameParameters.Add(TEXT("Mode"), TEXT("Max"));
		StructuralMass.NumericParameters.Add(TEXT("Ratio"), bAlpine ? 0.62 : 0.52);
		StructuralMass.NameParameters.Add(TEXT("Output"), TEXT("Clamp"));
		Link(Recipe, MassCells, TEXT("Out"), StructuralMass, TEXT("Input1"));
		Link(Recipe, Peaks, TEXT("Out"), StructuralMass, TEXT("Input2"));

		FGaeaTerrainNode& MacroRidges = AddNode(Recipe, GaeaTerrainNodeTypes::Voronoi, Seed, Ordinal++);
		MacroRidges.NumericParameters.Add(TEXT("Scale"), FMath::Clamp(StructureScale * 0.63f, 0.02f, 16.0f));
		MacroRidges.NumericParameters.Add(TEXT("Jitter"), bAlpine ? 1.22 : 1.12);
		MacroRidges.NameParameters.Add(TEXT("Form"), TEXT("R"));
		MacroRidges.NumericParameters.Add(TEXT("Gain"), bAlpine ? 1.32 : 1.12);
		MacroRidges.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Seed) + 83);
		MacroRidges.NameParameters.Add(TEXT("WarpType"), TEXT("Complex"));
		MacroRidges.NumericParameters.Add(TEXT("WarpFrequency"), bAlpine ? 0.78 : 0.62);
		MacroRidges.NumericParameters.Add(TEXT("WarpAmplitude"), bAlpine ? 0.54 : 0.46);
		MacroRidges.IntegerParameters.Add(TEXT("WarpOctaves"), bReduceDetails ? 2 : 4);

		FGaeaTerrainNode& FineRidges = AddNode(Recipe, GaeaTerrainNodeTypes::Voronoi, Seed, Ordinal++);
		FineRidges.NumericParameters.Add(TEXT("Scale"), RidgeScale);
		FineRidges.NumericParameters.Add(TEXT("Jitter"), bAlpine ? 1.26 : 1.16);
		FineRidges.NameParameters.Add(TEXT("Form"), TEXT("D"));
		FineRidges.NumericParameters.Add(TEXT("Gain"), bAlpine ? 1.40 : 1.18);
		FineRidges.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Seed) + 149);
		FineRidges.NameParameters.Add(TEXT("WarpType"), TEXT("Complex"));
		FineRidges.NumericParameters.Add(TEXT("WarpFrequency"), bAlpine ? 0.92 : 0.76);
		FineRidges.NumericParameters.Add(TEXT("WarpAmplitude"), bAlpine ? 0.60 : 0.50);
		FineRidges.IntegerParameters.Add(TEXT("WarpOctaves"), bReduceDetails ? 2 : 4);

		FGaeaTerrainNode& RidgeNetwork = AddNode(Recipe, GaeaTerrainNodeTypes::Combine, Seed, Ordinal++);
		RidgeNetwork.NameParameters.Add(TEXT("Mode"), TEXT("Max"));
		RidgeNetwork.NumericParameters.Add(TEXT("Ratio"), bAlpine ? 0.58 : 0.46);
		RidgeNetwork.NameParameters.Add(TEXT("Output"), TEXT("Clamp"));
		Link(Recipe, MacroRidges, TEXT("Out"), RidgeNetwork, TEXT("Input1"));
		Link(Recipe, FineRidges, TEXT("Out"), RidgeNetwork, TEXT("Input2"));

		FGaeaTerrainNode& StructuredWithRidges = AddNode(Recipe, GaeaTerrainNodeTypes::Combine, Seed, Ordinal++);
		StructuredWithRidges.NameParameters.Add(TEXT("Mode"), TEXT("Max"));
		StructuredWithRidges.NumericParameters.Add(TEXT("Ratio"), bAlpine ? 0.38 : 0.26);
		StructuredWithRidges.NameParameters.Add(TEXT("Output"), TEXT("Clamp"));
		Link(Recipe, StructuralMass, TEXT("Out"), StructuredWithRidges, TEXT("Input1"));
		Link(Recipe, RidgeNetwork, TEXT("Out"), StructuredWithRidges, TEXT("Input2"));

		// The support field now controls footprint only. Structural fields control
		// the actual mountain surface; keeping this ratio high prevents the smooth
		// support envelope from re-emerging as the visible dome.
		FGaeaTerrainNode& Base = AddNode(Recipe, GaeaTerrainNodeTypes::Combine, Seed, Ordinal++);
		Base.NameParameters.Add(TEXT("Mode"), TEXT("Multiply"));
		Base.NumericParameters.Add(TEXT("Ratio"), bAlpine ? 0.94 : 0.90);
		Base.NameParameters.Add(TEXT("Output"), TEXT("Clamp"));
		Link(Recipe, Support, TEXT("Out"), Base, TEXT("Input1"));
		Link(Recipe, StructuredWithRidges, TEXT("Out"), Base, TEXT("Input2"));

		FGaeaTerrainNode& Shaper = AddNode(Recipe, GaeaTerrainNodeTypes::Shaper, Seed, Ordinal++);
		Shaper.NumericParameters.Add(TEXT("Shape"), bAlpine ? 0.085 : 0.035);
		Shaper.NumericParameters.Add(TEXT("LocalEffect"), bAlpine ? 0.22 : 0.16);
		Shaper.NumericParameters.Add(TEXT("LocalArea"), bAlpine ? 0.46 : 0.54);
		Shaper.BoolParameters.Add(TEXT("MaintainFineDetails"), true);
		Shaper.NumericParameters.Add(TEXT("DetailSize"), bReduceDetails ? 0.52 : bAlpine ? 0.18 : 0.22);
		Link(Recipe, Base, TEXT("Out"), Shaper, TEXT("Terrain"));

		FGaeaTerrainNode& SlopeWarp = AddNode(Recipe, GaeaTerrainNodeTypes::SlopeWarp, Seed, Ordinal++);
		SlopeWarp.NumericParameters.Add(TEXT("Intensity"), bAlpine ? 0.17 : bStrata ? 0.13 : 0.11);
		SlopeWarp.IntegerParameters.Add(TEXT("Iterations"), bReduceDetails ? 1 : bAlpine ? 2 : 1);
		SlopeWarp.NumericParameters.Add(TEXT("Direction"), bStrata ? 22.0 : 0.0);
		// This is critical: normalized SlopeWarp displaced every non-flat Voronoi
		// gradient by almost the same amount, producing the giant smooth vertical
		// flutes visible in the previous Mountain output. Let gradient magnitude
		// control displacement so major ridges move more than incidental texture.
		SlopeWarp.BoolParameters.Add(TEXT("Normalized"), false);
		SlopeWarp.NameParameters.Add(TEXT("Quality"), bReduceDetails ? TEXT("Medium") : TEXT("High"));
		SlopeWarp.NameParameters.Add(TEXT("Antialiasing"), bReduceDetails ? TEXT("Off") : TEXT("x4"));
		Link(Recipe, Shaper, TEXT("Out"), SlopeWarp, TEXT("Input"));
		Link(Recipe, RidgeNetwork, TEXT("Out"), SlopeWarp, TEXT("Guide"));

		FGaeaTerrainNode* LastProcess = &SlopeWarp;

		// ---- Style branches ----------------------------------------------------
		// Basic stops here. Eroded and Old are the same underlying Basic massif
		// with increasing geomorphic age. Alpine uses the alternate structural
		// field above and receives a lighter glacial-looking process pass.
		if (bEroded || bOld || bAlpine)
		{
			FGaeaTerrainNode& Macro = AddNode(Recipe, GaeaTerrainNodeTypes::Erosion2, Seed, Ordinal++);
			Macro.IntegerParameters.Add(TEXT("Duration"), bReduceDetails ? 12 : bOld ? 58 : bEroded ? 34 : 30);
			Macro.NumericParameters.Add(TEXT("Downcutting"), bOld ? 0.48 : bEroded ? 0.58 : 0.70);
			Macro.NumericParameters.Add(TEXT("ErosionScale"), MacroErosionMeters);
			Macro.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Seed) + 307);
			Macro.NumericParameters.Add(TEXT("SuspendedLoad"), bOld ? 0.72 : bEroded ? 0.60 : 0.52);
			Macro.NumericParameters.Add(TEXT("BedLoad"), bOld ? 0.62 : bEroded ? 0.48 : 0.42);
			Macro.NumericParameters.Add(TEXT("CoarseSediments"), bOld ? 0.44 : bEroded ? 0.30 : 0.32);
			Macro.NumericParameters.Add(TEXT("Shape"), bOld ? 0.56 : bEroded ? 0.48 : 0.64);
			Macro.NumericParameters.Add(TEXT("ShapeSharpness"), bOld ? 0.38 : bEroded ? 0.50 : 0.72);
			Macro.NumericParameters.Add(TEXT("ShapeDetailScale"), bReduceDetails ? 0.42 : bAlpine ? 0.92 : 0.76);
			Macro.BoolParameters.Add(TEXT("EnableOrographic"), bAlpine);
			Macro.NumericParameters.Add(TEXT("Direction"), 25.0);
			Macro.NumericParameters.Add(TEXT("DirectionalPrecipitation"), 0.42);
			Macro.NumericParameters.Add(TEXT("RainShadow"), 0.12);
			Link(Recipe, *LastProcess, TEXT("Out"), Macro, TEXT("Terrain"));
			LastProcess = &Macro;

			if (!bReduceDetails)
			{
				FGaeaTerrainNode& Fine = AddNode(Recipe, GaeaTerrainNodeTypes::Erosion2, Seed, Ordinal++);
				Fine.IntegerParameters.Add(TEXT("Duration"), bOld ? 34 : bEroded ? 18 : 14);
				Fine.NumericParameters.Add(TEXT("Downcutting"), bOld ? 0.36 : bEroded ? 0.44 : 0.58);
				Fine.NumericParameters.Add(TEXT("ErosionScale"), FineErosionMeters);
				Fine.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Seed) + 503);
				Fine.NumericParameters.Add(TEXT("SuspendedLoad"), bOld ? 0.56 : 0.38);
				Fine.NumericParameters.Add(TEXT("BedLoad"), bOld ? 0.48 : 0.30);
				Fine.NumericParameters.Add(TEXT("CoarseSediments"), bOld ? 0.34 : 0.18);
				Fine.NumericParameters.Add(TEXT("Shape"), bOld ? 0.44 : bAlpine ? 0.34 : 0.30);
				Fine.NumericParameters.Add(TEXT("ShapeSharpness"), bOld ? 0.30 : bAlpine ? 0.58 : 0.46);
				Fine.NumericParameters.Add(TEXT("ShapeDetailScale"), bAlpine ? 0.86 : 0.68);
				Link(Recipe, *LastProcess, TEXT("Out"), Fine, TEXT("Terrain"));
				LastProcess = &Fine;
			}

			FGaeaTerrainNode& Thermal = AddNode(Recipe, GaeaTerrainNodeTypes::Thermal2, Seed, Ordinal++);
			Thermal.IntegerParameters.Add(TEXT("Duration"), bReduceDetails ? 4 : bOld ? 22 : bEroded ? 10 : 8);
			Thermal.NumericParameters.Add(TEXT("Strength"), bOld ? 0.44 : bEroded ? 0.24 : 0.18);
			Thermal.NumericParameters.Add(TEXT("Anisotropy"), bAlpine ? 0.12 : 0.03);
			Thermal.NumericParameters.Add(TEXT("Angle"), bAlpine ? 41.0 : bOld ? 32.0 : 35.0);
			Thermal.NumericParameters.Add(TEXT("SedimentRemoval"), bAlpine ? 0.32 : bOld ? 0.20 : 0.12);
			Thermal.NumericParameters.Add(TEXT("FeatureScale"), ThermalFeatureMeters);
			Link(Recipe, *LastProcess, TEXT("Out"), Thermal, TEXT("Terrain"));
			LastProcess = &Thermal;
		}
		else if (bStrata)
		{
			FGaeaTerrainNode& Stratify = AddNode(Recipe, GaeaTerrainNodeTypes::Stratify, Seed, Ordinal++);
			Stratify.NumericParameters.Add(TEXT("Spacing"), bReduceDetails ? 0.42 : 0.24);
			Stratify.IntegerParameters.Add(TEXT("Octaves"), bReduceDetails ? 3 : 6);
			Stratify.NumericParameters.Add(TEXT("Intensity"), bReduceDetails ? 0.34 : 0.62);
			Stratify.NumericParameters.Add(TEXT("Shape"), 0.72);
			Stratify.IntegerParameters.Add(TEXT("Seed"), static_cast<int64>(Seed) + 911);
			Stratify.NumericParameters.Add(TEXT("TiltAmount"), 0.28);
			Stratify.NumericParameters.Add(TEXT("Direction"), 26.0);
			Link(Recipe, *LastProcess, TEXT("Out"), Stratify, TEXT("Terrain"));
			LastProcess = &Stratify;
		}

		Recipe.OutputNode = LastProcess->Id;

		FGaeaTerrainEvaluationContext InnerContext = Context;
		const FGaeaTerrainEvaluationResult Evaluation = FGaeaTerrainEvaluator::Evaluate(Recipe, InnerContext);
		if (!Evaluation.bSuccess)
		{
			Error = FString::Printf(TEXT("Mountain audited composite failed: %s"), *Evaluation.Error);
			return false;
		}

		FGaeaTerrainDataset Dataset = Evaluation.Dataset;
		const float HeightScale = Evaluation.HeightScale;
		if (!ConstrainMountainHeight(Dataset, RequestedHeight, Error)) return false;
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
	D.Description = TEXT("Gaea-style hierarchical mountain macro: Basic authors an asymmetric massif and ridge network; Eroded and Old progressively weather it; Alpine selects a distinct sharp-ridge form; Strata applies directional exposed-rock structure.");
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
	D.Parameters.Add(Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.01, 2.0, TEXT("Mountain")));
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
