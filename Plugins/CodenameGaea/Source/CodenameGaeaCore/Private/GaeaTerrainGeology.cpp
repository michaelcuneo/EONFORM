#include "GaeaTerrainGeology.h"

#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNoise.h"
#include "GaeaTerrainShaping.h"

namespace
{
	FGaeaScalarField MakeField(const FGaeaGridDomain& Domain, FName Name)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;

		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor);
		return Field;
	}
}

bool FGaeaTerrainGeology::Build(
	const FGaeaScalarField& Height,
	int32 Seed,
	const FGaeaTerrainGeologySettings& Settings,
	FGaeaTerrainDataset& InOutDataset,
	FString* OutError)
{
	auto Fail = [OutError](const TCHAR* Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (!Height.IsValid()) return Fail(TEXT("Geology requires a valid Height field."));

	const FGaeaScalarField* Mountain = InOutDataset.FindScalarField(GaeaTerrainFieldNames::Mountain);
	const FGaeaScalarField* Plains = InOutDataset.FindScalarField(GaeaTerrainFieldNames::Plains);
	const FGaeaScalarField* Slope = InOutDataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
	const FGaeaScalarField* Concavity = InOutDataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
	const FGaeaScalarField* Required[] = { Mountain, Plains, Slope, Concavity };
	for (const FGaeaScalarField* Field : Required)
	{
		if (!Field || !Field->IsValid() || Field->Domain != Height.Domain)
		{
			return Fail(TEXT("Geology requires Terrain Context fields on the same domain as Height."));
		}
	}

	FGaeaFractalNoiseSettings NoiseSettings;
	NoiseSettings.Frequency = FMath::Max(Settings.Frequency, 0.000001f);
	NoiseSettings.Octaves = FMath::Clamp(Settings.Octaves, 1, 16);
	NoiseSettings.Persistence = 0.5f;
	NoiseSettings.Lacunarity = 2.0f;
	const FVector2D GeologyOffset = FGaeaTerrainNoise::MakeSeedOffset(Seed, 707);

	FGaeaScalarField RockHardness = MakeField(Height.Domain, GaeaTerrainFieldNames::RockHardness);
	FGaeaScalarField Weathering = MakeField(Height.Domain, GaeaTerrainFieldNames::Weathering);
	FGaeaScalarField SoilDepth = MakeField(Height.Domain, GaeaTerrainFieldNames::SoilDepth);

	for (int32 Y = 0; Y < Height.Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Height.Domain.Dimensions.X; ++X)
		{
			const FVector2d World = Height.Domain.InteriorSampleToWorld(X, Y);
			float Lithology = FGaeaTerrainNoise::SampleFractal(
				FVector2D(static_cast<float>(World.X), static_cast<float>(World.Y)),
				GeologyOffset,
				NoiseSettings);
			Lithology = FGaeaTerrainShaping::ApplySignedPower(Lithology, Settings.Contrast);
			Lithology = FMath::Clamp(Lithology * 0.5f + 0.5f, 0.0f, 1.0f);

			const float MountainValue = Mountain->AtInterior(X, Y);
			const float PlainsValue = Plains->AtInterior(X, Y);
			const float SlopeValue = Slope->AtInterior(X, Y);
			const float ConcavityValue = Concavity->AtInterior(X, Y);

			const float Hardness = FMath::Clamp(
				Lithology
				+ MountainValue * Settings.MountainHardnessBias
				- PlainsValue * Settings.PlainsSoftnessBias,
				0.0f,
				1.0f);
			RockHardness.AtInterior(X, Y) = Hardness;

			const float WeatheringValue = FMath::Clamp(
				(1.0f - Hardness) * 0.65f
				+ ConcavityValue * 0.1f
				+ (1.0f - FMath::Clamp(SlopeValue / 45.0f, 0.0f, 1.0f)) * 0.05f,
				0.0f,
				1.0f);
			Weathering.AtInterior(X, Y) = WeatheringValue;

			const float StableSurface = 1.0f - FMath::Clamp(SlopeValue / 35.0f, 0.0f, 1.0f);
			SoilDepth.AtInterior(X, Y) = FMath::Clamp(
				WeatheringValue
				* StableSurface
				* (0.4f + PlainsValue * 0.35f + ConcavityValue * 0.2f)
				* Settings.SoilFormationStrength,
				0.0f,
				1.0f);
		}
	}

	if (!InOutDataset.SetScalarField(MoveTemp(RockHardness))
		|| !InOutDataset.SetScalarField(MoveTemp(Weathering))
		|| !InOutDataset.SetScalarField(MoveTemp(SoilDepth)))
	{
		return Fail(TEXT("Geology could not publish its derived fields."));
	}

	if (OutError) OutError->Reset();
	return true;
}
