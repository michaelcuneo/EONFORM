#include "EonformRidgeNode.h"

#include "EonformDirectionalWarpNode.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainFractalWarp.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainProceduralOps.h"
#include "EonformTerrainRawNoise.h"
#include "EonformTerrainRecipe.h"
#include "EonformTerrainTerrace.h"

namespace EonformTerrainNodeTypes
{
	const FName Ridge(TEXT("Ridge"));
}

namespace
{
	FEonformTerrainPortDescriptor RidgeTerrainOut()
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = TEXT("Out");
		Port.DisplayName = TEXT("Out");
		Port.DataType = TEXT("Terrain");
		return Port;
	}

	FEonformTerrainParameterDescriptor RidgeNumber(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
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

	FEonformTerrainParameterDescriptor RidgeInteger(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
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
		P.Maximum = static_cast<double>(Max);
		return P;
	}

	FEonformGridDomain BuildRidgeDomain(const FEonformTerrainEvaluationContext& Context)
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
		return FEonformGridDomain::Make(
			FIntPoint(Width, Height),
			FVector2d(-WorldWidthCm * 0.5, -WorldDepthCm * 0.5),
			FVector2d(WorldWidthCm * 0.5, WorldDepthCm * 0.5));
	}

	float ResolveRidgeHeightScale(const FEonformTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale())
		{
			return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		}
		return FMath::Max(Context.HeightScale, 1.0f);
	}

	bool EvaluateRidgeNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs&,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformGridDomain Domain = BuildRidgeDomain(Context);
		if (!Domain.IsValid())
		{
			Error = TEXT("Ridge produced an invalid evaluation domain.");
			return false;
		}

		FEonformRidgeSettings Settings;
		Settings.Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.75)), 0.0001f, 1.0f);
		Settings.Height = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 0.6)), 0.0f, 3.0f);
		Settings.Definition = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Definition"), 0.4)), 0.0f, 1.0f);
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 0));
		Settings.ScaleX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), 1.0)), 0.0001f, 100.0f);
		Settings.ScaleY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), 1.0)), 0.0001f, 100.0f);

		FEonformScalarField Height;
		if (!FEonformRidgeGenerator::Generate(Domain, Settings, Height, &Error)) return false;

		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Ridge could not publish Height.");
			return false;
		}
		FEonformTerrainValue Value = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), ResolveRidgeHeightScale(Context));
		if (!Value.IsValid())
		{
			Error = TEXT("Ridge produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Value));
		return true;
	}
}

bool FEonformRidgeGenerator::Generate(
	const FEonformGridDomain& Domain,
	const FEonformRidgeSettings& Settings,
	FEonformScalarField& OutHeight,
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

	constexpr float RidgeVoronoiScaleCoefficient = 1.05f;
	constexpr float RidgePerlinScale = 0.75f;
	constexpr int32 RidgePerlinOctaves = 12;
	constexpr float RidgePerlinGain = 0.5f;
	constexpr int32 RidgeTerraceCount = 67;
	constexpr float RidgeTerraceUniformity = 0.6f;
	constexpr float RidgeTerraceSteepness = 0.2f;
	constexpr float RidgeTerraceIntensity = 0.8f;
	constexpr float GuideWarpDefinitionCoefficient = 0.99f;
	constexpr float DirectionStrengthCoefficient = 1.25f;
	constexpr float SecondaryWarpStrength = 0.65f;

	EonformTerrainProceduralOps::FVoronoiSettings VoronoiSettings;
	VoronoiSettings.Scale = 1.0f - Scale * RidgeVoronoiScaleCoefficient;
	VoronoiSettings.Function = TEXT("Euclidean");
	VoronoiSettings.Form = TEXT("P");
	VoronoiSettings.Gain = 0.5f;
	VoronoiSettings.WarpType = TEXT("Complex");
	VoronoiSettings.WarpFrequency = 0.1f;
	VoronoiSettings.WarpAmplitude = 0.3f;
	VoronoiSettings.WarpOctaves = 10;
	VoronoiSettings.Seed = Settings.Seed;
	VoronoiSettings.ScaleX = Settings.ScaleX;
	VoronoiSettings.ScaleY = Settings.ScaleY;
	VoronoiSettings.Jitter = 0.45f;

	FEonformScalarField Structure;
	if (!EonformTerrainRawNoise::Voronoi(Domain, VoronoiSettings, Structure, OutError)) return false;

	EonformTerrainProceduralOps::FPerlinSettings PerlinSettings;
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

	FEonformScalarField Control;
	if (!EonformTerrainRawNoise::Perlin(Domain, PerlinSettings, Control, OutError)) return false;

	FEonformScalarField Terraced;
	if (!EonformTerrainProceduralOps::TerraceFidelity(
		Control,
		RidgeTerraceCount,
		RidgeTerraceUniformity,
		RidgeTerraceSteepness,
		RidgeTerraceIntensity,
		Settings.Seed + 3,
		Terraced,
		OutError)) return false;

	EonformTerrainProceduralOps::FFractalWarpSettings GuideWarpSettings;
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
	GuideWarpSettings.EdgeBehaviour = EonformTerrainProceduralOps::EEdgeBehaviour::Edge;
	GuideWarpSettings.Seed = Settings.Seed + 2;

	FEonformScalarField WarpedGuide;
	if (!EonformTerrainProceduralOps::FractalWarpFidelity(Terraced, GuideWarpSettings, WarpedGuide, OutError)) return false;
	FEonformScalarField Guide = Terraced;
	for (int32 I = 0; I < Guide.Values.Num(); ++I)
	{
		Guide.Values[I] = FMath::Max(Terraced.Values[I], WarpedGuide.Values[I]);
	}

	FEonformScalarField Directed;
	const float DirectionStrengthPixels = Definition * DirectionStrengthCoefficient * static_cast<float>(Domain.Dimensions.X);
	if (!EonformDirectionalWarp::ApplyPixels(
		Structure,
		Guide,
		DirectionStrengthPixels,
		45.0f,
		EonformDirectionalWarp::EEdgeBehaviour::Mirror,
		Directed,
		OutError)) return false;

	EonformTerrainProceduralOps::FFractalWarpSettings SecondaryWarpSettings;
	SecondaryWarpSettings.Size = FMath::Max(Scale / 2.0f, 0.0001f);
	SecondaryWarpSettings.Strength = SecondaryWarpStrength;
	SecondaryWarpSettings.bPersistStrength = true;
	SecondaryWarpSettings.ZScale = 0.0f;
	SecondaryWarpSettings.NoiseType = TEXT("Voronoi R");
	SecondaryWarpSettings.Perturbation = 0.4f;
	SecondaryWarpSettings.Octaves = 12;
	SecondaryWarpSettings.Roughness = 0.5f;
	SecondaryWarpSettings.bNormalized = false;
	SecondaryWarpSettings.Iterations = 1;
	SecondaryWarpSettings.Mode = TEXT("Vector Field");
	SecondaryWarpSettings.EdgeBehaviour = EonformTerrainProceduralOps::EEdgeBehaviour::Mirror;
	SecondaryWarpSettings.Seed = Settings.Seed + 5;

	FEonformScalarField SecondaryWarped;
	if (!EonformTerrainProceduralOps::FractalWarpFidelity(Directed, SecondaryWarpSettings, SecondaryWarped, OutError)) return false;

	OutHeight = Directed;
	for (int32 I = 0; I < OutHeight.Values.Num(); ++I)
	{
		OutHeight.Values[I] = FMath::Min(Directed.Values[I], SecondaryWarped.Values[I]);
	}

	float MinValue = TNumericLimits<float>::Max();
	for (const float Value : OutHeight.Values)
	{
		MinValue = FMath::Min(Value, MinValue);
	}
	for (float& Value : OutHeight.Values)
	{
		Value = FMath::Clamp(Value - MinValue, 0.0f, 1.0f);
		Value *= Settings.Height;
	}
	OutHeight.Descriptor.Name = EonformTerrainFieldNames::Height;

	if (OutError) OutError->Reset();
	return OutHeight.IsValid();
}

void RegisterEonformRidgeNode()
{
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Ridge;
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
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(D.Type, EvaluateRidgeNode);
}
