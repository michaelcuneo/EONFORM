#include "TerrainGeology.h"

#include "TerrainContext.h"
#include "TerrainNoise.h"
#include "TerrainShaping.h"
#include "TerrainStructure.h"

namespace
{
	void InitializeNormalizedField(FGaeaScalarField& Field, const FGaeaGridDomain& Domain, FName Name)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		Field.Initialize(Domain, Descriptor);
	}
}

void FTerrainGeology::Build(
	const FTerrainHeightField& HeightField,
	const FTerrainContextMaps& Context,
	const FTerrainStructuralMaps* Structure,
	int32 Seed,
	const FTerrainGeologySettings& Settings,
	FTerrainGeologyMaps& OutGeology)
{
	OutGeology = FTerrainGeologyMaps{};
	if (!HeightField.IsValid() || !Context.IsValidFor(HeightField))
	{
		return;
	}

	const int32 Resolution = HeightField.Resolution;
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const float HalfWorldSize = HeightField.WorldSize * 0.5f;
	const bool bHasStructure = Structure && Structure->IsValidFor(HeightField);
	const FGaeaGridDomain& Domain = HeightField.GetGaeaDomain();

	InitializeNormalizedField(OutGeology.RockHardnessField, Domain, TEXT("RockHardness"));
	InitializeNormalizedField(OutGeology.WeatheringField, Domain, TEXT("Weathering"));
	InitializeNormalizedField(OutGeology.SoilDepthField, Domain, TEXT("SoilDepth"));

	FTerrainFractalNoiseSettings GeologyNoiseSettings;
	GeologyNoiseSettings.Frequency = Settings.Frequency;
	GeologyNoiseSettings.Octaves = Settings.Octaves;
	GeologyNoiseSettings.Persistence = 0.5f;
	GeologyNoiseSettings.Lacunarity = 2.0f;

	const FVector2D GeologyOffset = FTerrainNoise::MakeSeedOffset(Seed, 707);

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 Index = HeightField.Index(X, Y);
			const FVector2D WorldPosition(
				static_cast<float>(X) * CellSize - HalfWorldSize,
				static_cast<float>(Y) * CellSize - HalfWorldSize);

			float Lithology = FTerrainNoise::SampleFractal(WorldPosition, GeologyOffset, GeologyNoiseSettings);
			Lithology = FTerrainShaping::ApplySignedPower(Lithology, Settings.Contrast);
			Lithology = FMath::Clamp(Lithology * 0.5f + 0.5f, 0.0f, 1.0f);

			const float Mountain = Context.Mountain[Index];
			const float Plains = Context.Plains[Index];
			const float Slope = Context.SlopeDegrees[Index];
			const float Concavity = Context.Concavity[Index];
			const float FaultWeakness = bHasStructure ? Structure->FaultWeakness[Index] : 0.0f;
			const float Bedding = bHasStructure ? Structure->Bedding[Index] : 0.5f;

			const float BeddingBias = (Bedding - 0.5f) * Settings.BeddingHardnessContrast * 2.0f;
			const float Hardness = FMath::Clamp(
				Lithology
				+ Mountain * Settings.MountainHardnessBias
				- Plains * Settings.PlainsSoftnessBias
				+ BeddingBias
				- FaultWeakness * Settings.FaultWeakeningStrength,
				0.0f,
				1.0f);
			OutGeology.RockHardness[Index] = Hardness;

			const float Weathering = FMath::Clamp(
				(1.0f - Hardness) * 0.65f
				+ FaultWeakness * 0.2f
				+ Concavity * 0.1f
				+ (1.0f - FMath::Clamp(Slope / 45.0f, 0.0f, 1.0f)) * 0.05f,
				0.0f,
				1.0f);
			OutGeology.Weathering[Index] = Weathering;

			const float StableSurface = 1.0f - FMath::Clamp(Slope / 35.0f, 0.0f, 1.0f);
			const float SoilDepth = FMath::Clamp(
				Weathering
				* StableSurface
				* (0.4f + Plains * 0.35f + Concavity * 0.2f + FaultWeakness * 0.05f)
				* Settings.SoilFormationStrength,
				0.0f,
				1.0f);
			OutGeology.SoilDepth[Index] = SoilDepth;
		}
	}
}
