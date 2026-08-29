#include "EonformRidgeNode.h"

#include "Async/ParallelFor.h"
#include "EonformCombineNode.h"
#include "EonformDirectionalWarpNode.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainFractalWarp.h"
#include "EonformTerrainProceduralOps.h"
#include "EonformTerrainRawNoise.h"
#include "EonformTerrainTerrace.h"

namespace
{
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

	uint64 PointKey(int32 X, int32 Y)
	{
		return (static_cast<uint64>(static_cast<uint32>(Y)) << 32)
			| static_cast<uint64>(static_cast<uint32>(X));
	}

	FVector2D WorldToReferenceCoordinate(const FVector2d& World, const FEonformGridDomain& ReferenceDomain)
	{
		const FVector2d Size = ReferenceDomain.WorldSize();
		return FVector2D(
			static_cast<float>((World.X - ReferenceDomain.WorldMin.X) / Size.X * static_cast<double>(ReferenceDomain.Dimensions.X - 1)),
			static_cast<float>((World.Y - ReferenceDomain.WorldMin.Y) / Size.Y * static_cast<double>(ReferenceDomain.Dimensions.Y - 1)));
	}

	class FRidgePointEvaluator
	{
	public:
		FRidgePointEvaluator(
			const FIntPoint& InDimensions,
			const FEonformRidgeSettings& InSettings,
			bool bInEnableCaches,
			FString* OutError)
			: Dimensions(InDimensions)
			, Settings(InSettings)
			, Scale(FMath::Clamp(InSettings.Scale, 0.0001f, 1.0f))
			, Definition(FMath::Clamp(InSettings.Definition, 0.0f, 1.0f))
			, bEnableCaches(bInEnableCaches)
		{
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

			bValid = EonformTerrainProceduralOps::PrepareTerraceProfile(
				RidgeTerraceCount,
				RidgeTerraceUniformity,
				RidgeTerraceSteepness,
				RidgeTerraceIntensity,
				Settings.Seed + 3,
				TerraceProfile,
				OutError);

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

			if (bValid)
			{
				FVector2D Probe;
				FString WarpError;
				bValid = EonformTerrainProceduralOps::FractalWarpVectorCoordinate(
					FVector2D::ZeroVector,
					Dimensions,
					GuideWarpSettings,
					Probe,
					&WarpError)
					&& EonformTerrainProceduralOps::FractalWarpVectorCoordinate(
						FVector2D::ZeroVector,
						Dimensions,
						SecondaryWarpSettings,
						Probe,
						&WarpError);
				if (!bValid && OutError)
				{
					*OutError = FString::Printf(TEXT("Ridge streamed warp contract is unavailable: %s"), *WarpError);
				}
			}
		}

		bool IsValid() const { return bValid; }

		float PreRange(int32 X, int32 Y)
		{
			check(bValid);
			const uint64 Key = PointKey(X, Y);
			if (bEnableCaches)
			{
				if (const float* Cached = PreRangeCache.Find(Key)) return *Cached;
			}
			const float Value = FMath::Clamp(
				EonformCombine::ApplyMode(Directed(X, Y), SecondaryWarped(X, Y), TEXT("Min")),
				0.0f,
				Scale);
			if (bEnableCaches) PreRangeCache.Add(Key, Value);
			return Value;
		}

		float Final(int32 X, int32 Y, float Minimum)
		{
			return FMath::Clamp(PreRange(X, Y) - Minimum, 0.0f, 1.0f) * Settings.Height;
		}

	private:
		float Structure(int32 X, int32 Y)
		{
			return EonformTerrainRawNoise::SampleVoronoiReference(
				FVector2d(static_cast<double>(X), static_cast<double>(Y)),
				Dimensions.X,
				VoronoiSettings);
		}

		float Terraced(int32 X, int32 Y)
		{
			const float Control = EonformTerrainRawNoise::SamplePerlinReference(
				FVector2d(static_cast<double>(X), static_cast<double>(Y)),
				Dimensions.X,
				PerlinSettings);
			return EonformTerrainProceduralOps::ApplyPreparedTerraceValue(Control, TerraceProfile);
		}

		float Guide(int32 X, int32 Y)
		{
			const uint64 Key = PointKey(X, Y);
			if (bEnableCaches)
			{
				if (const float* Cached = GuideCache.Find(Key)) return *Cached;
			}

			FVector2D SourceCoordinate;
			const bool bResolved = EonformTerrainProceduralOps::FractalWarpVectorCoordinate(
				FVector2D(X, Y),
				Dimensions,
				GuideWarpSettings,
				SourceCoordinate,
				nullptr);
			checkf(bResolved, TEXT("Validated Ridge guide FractalWarp failed during point evaluation."));
			const float Warped = EonformTerrainProceduralOps::FractalWarpSampleBilinear(
				[this](int32 SX, int32 SY) { return Terraced(SX, SY); },
				Dimensions,
				static_cast<float>(SourceCoordinate.X),
				static_cast<float>(SourceCoordinate.Y),
				GuideWarpSettings.EdgeBehaviour);
			const float Value = EonformCombine::ApplyMode(Terraced(X, Y), Warped, TEXT("Max"));
			if (bEnableCaches) GuideCache.Add(Key, Value);
			return Value;
		}

		float Directed(int32 X, int32 Y)
		{
			const uint64 Key = PointKey(X, Y);
			if (bEnableCaches)
			{
				if (const float* Cached = DirectedCache.Find(Key)) return *Cached;
			}

			const float StrengthPixels = Definition * DirectionStrengthCoefficient * static_cast<float>(Dimensions.X);
			const FVector2D SourceCoordinate = EonformDirectionalWarp::ResolveSourceCoordinate(
				FVector2D(X, Y),
				Guide(X, Y),
				StrengthPixels,
				45.0f);
			const float Value = EonformDirectionalWarp::SampleBilinear(
				[this](int32 SX, int32 SY) { return Structure(SX, SY); },
				Dimensions,
				static_cast<float>(SourceCoordinate.X),
				static_cast<float>(SourceCoordinate.Y),
				EonformDirectionalWarp::EEdgeBehaviour::Mirror);
			if (bEnableCaches) DirectedCache.Add(Key, Value);
			return Value;
		}

		float SecondaryWarped(int32 X, int32 Y)
		{
			FVector2D SourceCoordinate;
			const bool bResolved = EonformTerrainProceduralOps::FractalWarpVectorCoordinate(
				FVector2D(X, Y),
				Dimensions,
				SecondaryWarpSettings,
				SourceCoordinate,
				nullptr);
			checkf(bResolved, TEXT("Validated Ridge secondary FractalWarp failed during point evaluation."));
			return EonformTerrainProceduralOps::FractalWarpSampleBilinear(
				[this](int32 SX, int32 SY) { return Directed(SX, SY); },
				Dimensions,
				static_cast<float>(SourceCoordinate.X),
				static_cast<float>(SourceCoordinate.Y),
				SecondaryWarpSettings.EdgeBehaviour);
		}

		FIntPoint Dimensions;
		FEonformRidgeSettings Settings;
		float Scale = 1.0f;
		float Definition = 0.0f;
		bool bEnableCaches = false;
		bool bValid = false;
		EonformTerrainProceduralOps::FVoronoiSettings VoronoiSettings;
		EonformTerrainProceduralOps::FPerlinSettings PerlinSettings;
		EonformTerrainProceduralOps::FPreparedTerraceProfile TerraceProfile;
		EonformTerrainProceduralOps::FFractalWarpSettings GuideWarpSettings;
		EonformTerrainProceduralOps::FFractalWarpSettings SecondaryWarpSettings;
		TMap<uint64, float> GuideCache;
		TMap<uint64, float> DirectedCache;
		TMap<uint64, float> PreRangeCache;
	};

	bool ResolveExactMinimum(
		const FIntPoint& Dimensions,
		const FEonformRidgeSettings& Settings,
		const TSharedPtr<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe>& SummaryCache,
		uint64 SummaryKey,
		float& OutMinimum,
		FString* OutError)
	{
		if (SummaryCache.IsValid() && SummaryCache->Find(SummaryKey, OutMinimum)) return true;

		TArray<float> RowMinima;
		RowMinima.Init(1.0f, Dimensions.Y);
		TAtomic<bool> bFailed(false);
		ParallelFor(Dimensions.Y, [&](int32 Y)
		{
			if (bFailed.Load()) return;
			FString RowError;
			FRidgePointEvaluator Evaluator(Dimensions, Settings, false, &RowError);
			if (!Evaluator.IsValid())
			{
				bFailed.Store(true);
				return;
			}
			float RowMinimum = 1.0f;
			for (int32 X = 0; X < Dimensions.X; ++X)
			{
				RowMinimum = FMath::Min(RowMinimum, Evaluator.PreRange(X, Y));
			}
			RowMinima[Y] = RowMinimum;
		});

		if (bFailed.Load())
		{
			if (OutError) *OutError = TEXT("Ridge could not evaluate its exact full-world minimum because its streamed point contract failed validation.");
			return false;
		}

		OutMinimum = 1.0f;
		for (const float RowMinimum : RowMinima) OutMinimum = FMath::Min(OutMinimum, RowMinimum);
		if (SummaryCache.IsValid()) SummaryCache->Store(SummaryKey, OutMinimum);
		return true;
	}
}

bool FEonformRidgeGenerator::GenerateRegional(
	const FEonformGridDomain& TargetDomain,
	const FEonformGridDomain& RidgeReferenceDomain,
	const FEonformRidgeSettings& Settings,
	bool bPreviewEvaluation,
	const TSharedPtr<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe>& SummaryCache,
	uint64 SummaryKey,
	FEonformScalarField& OutHeight,
	FString* OutError)
{
	if (!TargetDomain.IsValid() || !RidgeReferenceDomain.IsValid())
	{
		if (OutError) *OutError = TEXT("Regional Ridge requires valid target and reference domains.");
		return false;
	}

	FRidgePointEvaluator Evaluator(RidgeReferenceDomain.Dimensions, Settings, true, OutError);
	if (!Evaluator.IsValid()) return false;

	FEonformFieldDescriptor Descriptor;
	Descriptor.Name = EonformTerrainFieldNames::Height;
	Descriptor.Unit = EEonformFieldUnit::Normalized;
	Descriptor.Interpolation = EEonformInterpolation::Bilinear;
	OutHeight.Initialize(TargetDomain, Descriptor, 0.0f);

	const FIntPoint Storage = TargetDomain.GetStorageDimensions();
	TArray<float> ReferenceX;
	TArray<float> ReferenceY;
	ReferenceX.SetNumUninitialized(Storage.X);
	ReferenceY.SetNumUninitialized(Storage.Y);
	for (int32 X = 0; X < Storage.X; ++X)
	{
		ReferenceX[X] = WorldToReferenceCoordinate(TargetDomain.StorageSampleToWorld(X, 0), RidgeReferenceDomain).X;
	}
	for (int32 Y = 0; Y < Storage.Y; ++Y)
	{
		ReferenceY[Y] = WorldToReferenceCoordinate(TargetDomain.StorageSampleToWorld(0, Y), RidgeReferenceDomain).Y;
	}

	float Minimum = 1.0f;
	if (bPreviewEvaluation)
	{
		for (int32 Y = 0; Y < Storage.Y; ++Y)
		{
			for (int32 X = 0; X < Storage.X; ++X)
			{
				const float PreRangeValue = EonformTerrainProceduralOps::FractalWarpSampleBilinear(
					[&Evaluator](int32 SX, int32 SY) { return Evaluator.PreRange(SX, SY); },
					RidgeReferenceDomain.Dimensions,
					ReferenceX[X],
					ReferenceY[Y],
					EonformTerrainProceduralOps::EEdgeBehaviour::Edge);
				OutHeight.AtStorage(X, Y) = PreRangeValue;
				Minimum = FMath::Min(Minimum, PreRangeValue);
			}
		}
		for (float& Value : OutHeight.Values)
		{
			Value = FMath::Clamp(Value - Minimum, 0.0f, 1.0f) * Settings.Height;
		}
	}
	else
	{
		if (!ResolveExactMinimum(
			RidgeReferenceDomain.Dimensions,
			Settings,
			SummaryCache,
			SummaryKey,
			Minimum,
			OutError)) return false;

		for (int32 Y = 0; Y < Storage.Y; ++Y)
		{
			for (int32 X = 0; X < Storage.X; ++X)
			{
				OutHeight.AtStorage(X, Y) = EonformTerrainProceduralOps::FractalWarpSampleBilinear(
					[&Evaluator, Minimum](int32 SX, int32 SY) { return Evaluator.Final(SX, SY, Minimum); },
					RidgeReferenceDomain.Dimensions,
					ReferenceX[X],
					ReferenceY[Y],
					EonformTerrainProceduralOps::EEdgeBehaviour::Edge);
			}
		}
	}

	if (OutError) OutError->Reset();
	return OutHeight.IsValid();
}
