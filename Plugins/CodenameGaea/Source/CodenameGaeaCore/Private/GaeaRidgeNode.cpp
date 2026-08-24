#include "GaeaRidgeNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainFractalWarp.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainProceduralOps.h"
#include "GaeaTerrainRawNoise.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaTerrainTerrace.h"

namespace GaeaTerrainNodeTypes
{
	const FName Ridge(TEXT("Ridge"));
}

namespace
{
	FGaeaTerrainPortDescriptor RidgeTerrainOut()
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = TEXT("Out");
		Port.DisplayName = TEXT("Out");
		Port.DataType = TEXT("Terrain");
		return Port;
	}

	FGaeaTerrainParameterDescriptor RidgeNumber(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
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

	FGaeaTerrainParameterDescriptor RidgeInteger(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
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

	FGaeaGridDomain BuildRidgeDomain(const FGaeaTerrainEvaluationContext& Context)
	{
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

	float ResolveRidgeHeightScale(const FGaeaTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale())
		{
			return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		}
		return FMath::Max(Context.HeightScale, 1.0f);
	}

	bool EvaluateRidgeNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaGridDomain Domain = BuildRidgeDomain(Context);
		if (!Domain.IsValid())
		{
			Error = TEXT("Ridge produced an invalid evaluation domain.");
			return false;
		}

		FGaeaRidgeSettings Settings;
		Settings.Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.75)), 0.0001f, 1.0f);
		Settings.Height = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 0.6)), 0.0f, 3.0f);
		Settings.Definition = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Definition"), 0.4)), 0.0f, 1.0f);
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 0));
		Settings.ScaleX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), 1.0)), 0.0001f, 100.0f);
		Settings.ScaleY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), 1.0)), 0.0001f, 100.0f);

		FGaeaScalarField Height;
		if (!FGaeaRidgeGenerator::Generate(Domain, Settings, Height, &Error)) return false;

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Ridge could not publish Height.");
			return false;
		}
		FGaeaTerrainValue Value = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), ResolveRidgeHeightScale(Context));
		if (!Value.IsValid())
		{
			Error = TEXT("Ridge produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Value));
		return true;
	}
}

bool FGaeaRidgeGenerator::Generate(
	const FGaeaGridDomain& Domain,
	const FGaeaRidgeSettings& Settings,
	FGaeaScalarField& OutHeight,
	FString* OutError)
{
	auto Fail = [OutError](const FString& Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (!Domain.IsValid()) return Fail(TEXT("Ridge requires a valid evaluation domain."));
	if (Domain.Dimensions.X < 2 || Domain.Dimensions.Y < 2) return Fail(TEXT("Ridge requires at least a 2x2 domain."));

	const float Scale = FMath::Clamp(Settings.Scale, 0.0001f, 1.0f);
	const float Definition = FMath::Clamp(Settings.Definition, 0.0f, 1.0f);

	constexpr float RidgeVoronoiScaleCoefficient = 1.05f; // e002(126)
	constexpr float RidgePerlinScale = 0.75f;             // e002(69)
	constexpr int32 RidgePerlinOctaves = 12;              // e000(13)
	constexpr float RidgePerlinGain = 0.5f;               // e002(2)
	constexpr int32 RidgeTerraceCount = 67;               // e000(29)
	constexpr float RidgeTerraceUniformity = 0.6f;        // e002(72)
	constexpr float RidgeTerraceSteepness = 0.2f;         // e002(82)
	constexpr float RidgeTerraceIntensity = 0.8f;         // e002(19)
	constexpr float GuideWarpDefinitionCoefficient = 0.99f; // e002(73)
	constexpr float DirectionStrengthCoefficient = 1.25f; // e002(127)
	constexpr float SecondaryWarpStrength = 0.65f;        // e002(102)

	GaeaTerrainProceduralOps::FVoronoiSettings VoronoiSettings;
	VoronoiSettings.Scale = 1.0f - Scale * RidgeVoronoiScaleCoefficient;
	VoronoiSettings.Function = TEXT("Euclidean");
	VoronoiSettings.Form = TEXT("P");
	VoronoiSettings.Gain = 0.5f;
	VoronoiSettings.WarpType = TEXT("Complex");
	VoronoiSettings.WarpFrequency = 0.1f; // e002(93)
	VoronoiSettings.WarpAmplitude = 0.3f; // e002(83)
	VoronoiSettings.WarpOctaves = 10;     // e000(11)
	VoronoiSettings.Seed = Settings.Seed;
	VoronoiSettings.ScaleX = Settings.ScaleX;
	VoronoiSettings.ScaleY = Settings.ScaleY;
	VoronoiSettings.Jitter = 0.45f;       // e002(106)

	FGaeaScalarField Structure;
	if (!GaeaTerrainRawNoise::Voronoi(Domain, VoronoiSettings, Structure, OutError)) return false;

	GaeaTerrainProceduralOps::FPerlinSettings PerlinSettings;
	PerlinSettings.Scale = RidgePerlinScale;
	PerlinSettings.Octaves = RidgePerlinOctaves;
	PerlinSettings.Gain = RidgePerlinGain;
	PerlinSettings.Type = TEXT("FBM");
	PerlinSettings.Seed = Settings.Seed + 1;
	PerlinSettings.WarpType = TEXT("None");
	PerlinSettings.WarpFrequency = 0.0f;
	PerlinSettings.WarpAmplitude = 0.0f;
	PerlinSettings.WarpOctaves = 1;
	PerlinSettings.ScaleX = 1.0f;
	PerlinSettings.ScaleY = 1.0f;

	FGaeaScalarField Control;
	if (!GaeaTerrainRawNoise::Perlin(Domain, PerlinSettings, Control, OutError)) return false;

	FGaeaScalarField Terraced;
	if (!GaeaTerrainProceduralOps::TerraceFidelity(
		Control,
		RidgeTerraceCount,
		RidgeTerraceUniformity,
		RidgeTerraceSteepness,
		RidgeTerraceIntensity,
		Settings.Seed + 3,
		Terraced,
		OutError)) return false;

	GaeaTerrainProceduralOps::FFractalWarpSettings GuideWarpSettings;
	GuideWarpSettings.Size = FMath::Max(1.0f - Definition * GuideWarpDefinitionCoefficient, 0.0001f);
	GuideWarpSettings.Strength = 1.0f;
	GuideWarpSettings.bPersistStrength = true;
	GuideWarpSettings.ZScale = 0.0f;
	GuideWarpSettings.NoiseType = TEXT("Perlin FBM");
	GuideWarpSettings.Perturbation = 0.5f;
	GuideWarpSettings.Octaves = 12;
	GuideWarpSettings.Roughness = 0.5f;
	GuideWarpSettings.bNormalized = false;
	GuideWarpSettings.Iterations = 1;
	GuideWarpSettings.Mode = TEXT("Vector Field");
	GuideWarpSettings.EdgeBehaviour = GaeaTerrainProceduralOps::EEdgeBehaviour::Edge;
	GuideWarpSettings.Seed = Settings.Seed + 2;

	FGaeaScalarField WarpedGuide;
	if (!GaeaTerrainProceduralOps::FractalWarpFidelity(Terraced, GuideWarpSettings, WarpedGuide, OutError)) return false;
	FGaeaScalarField Guide = Terraced;
	for (int32 I = 0; I < Guide.Values.Num(); ++I)
	{
		Guide.Values[I] = FMath::Max(Terraced.Values[I], WarpedGuide.Values[I]);
	}

	FGaeaScalarField Directed;
	const float DirectionStrengthPixels = Definition * DirectionStrengthCoefficient * static_cast<float>(Domain.Dimensions.X);
	if (!GaeaTerrainProceduralOps::DirectionWarpPixels(
		Structure,
		Guide,
		DirectionStrengthPixels,
		45.0f,
		GaeaTerrainProceduralOps::EEdgeBehaviour::Mirror,
		Directed,
		OutError)) return false;

	GaeaTerrainProceduralOps::FFractalWarpSettings SecondaryWarpSettings;
	SecondaryWarpSettings.Size = FMath::Max(Scale / 2.0f, 0.0001f);
	SecondaryWarpSettings.Strength = SecondaryWarpStrength;
	SecondaryWarpSettings.bPersistStrength = true;
	SecondaryWarpSettings.ZScale = 0.0f;
	SecondaryWarpSettings.NoiseType = TEXT("Voronoi R");
	SecondaryWarpSettings.Perturbation = 0.4f; // e002(74)
	SecondaryWarpSettings.Octaves = 12;
	SecondaryWarpSettings.Roughness = 0.5f;
	SecondaryWarpSettings.bNormalized = false;
	SecondaryWarpSettings.Iterations = 1;
	SecondaryWarpSettings.Mode = TEXT("Vector Field");
	SecondaryWarpSettings.EdgeBehaviour = GaeaTerrainProceduralOps::EEdgeBehaviour::Mirror;
	SecondaryWarpSettings.Seed = Settings.Seed + 5;

	FGaeaScalarField SecondaryWarped;
	if (!GaeaTerrainProceduralOps::FractalWarpFidelity(Directed, SecondaryWarpSettings, SecondaryWarped, OutError)) return false;

	OutHeight = Directed;
	float MinValue = 1.0f;
	for (int32 I = 0; I < OutHeight.Values.Num(); ++I)
	{
		OutHeight.Values[I] = FMath::Min(Directed.Values[I], SecondaryWarped.Values[I]);
		OutHeight.Values[I] = FMath::Clamp(OutHeight.Values[I], 0.0f, Scale);
		MinValue = FMath::Min(MinValue, OutHeight.Values[I]);
	}

	// Gaea f1a7 finds the field minimum and delegates to f1a8 with that minimum
	// and 1.0 as the normalization range. This is a standard [min, 1] -> [0, 1]
	// remap, not the previous divide-by-current-maximum implementation.
	const float Range = 1.0f - MinValue;
	if (Range > UE_SMALL_NUMBER)
	{
		const float InvRange = 1.0f / Range;
		for (float& Value : OutHeight.Values)
		{
			Value = (Value - MinValue) * InvRange;
		}
	}
	else
	{
		for (float& Value : OutHeight.Values) Value = 0.0f;
	}

	for (float& Value : OutHeight.Values)
	{
		Value *= Settings.Height;
	}
	OutHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

	if (OutError) OutError->Reset();
	return OutHeight.IsValid();
}

void RegisterGaeaRidgeNode()
{
	FGaeaTerrainNodeDescriptor D;
	D.Type = GaeaTerrainNodeTypes::Ridge;
	D.DisplayName = TEXT("Ridge");
	D.Category = TEXT("Terrain");
	D.Description = TEXT("Generates terrain ridges from the shared Voronoi, Perlin, Terrace and Warp operations.");
	D.Outputs.Add(RidgeTerrainOut());
	D.Parameters = {
		RidgeNumber(TEXT("Scale"), TEXT("Scale"), 0.75, 0.0001, 1.0, TEXT("Ridge")),
		RidgeNumber(TEXT("Height"), TEXT("Height"), 0.6, 0.0, 3.0, TEXT("Ridge")),
		RidgeNumber(TEXT("Definition"), TEXT("Definition"), 0.4, 0.0, 1.0, TEXT("Ridge")),
		RidgeInteger(TEXT("Seed"), TEXT("Seed"), 0, -2147483647, 2147483647, TEXT("Ridge")),
		RidgeNumber(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.0001, 100.0, TEXT("Transform")),
		RidgeNumber(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.0001, 100.0, TEXT("Transform"))
	};
	FGaeaTerrainNodeDescriptorRegistry::Register(D);
	FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateRidgeNode);
}
