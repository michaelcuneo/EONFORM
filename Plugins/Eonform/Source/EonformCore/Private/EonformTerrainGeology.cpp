#include "EonformTerrainGeology.h"

#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNoise.h"
#include "EonformTerrainShaping.h"

namespace
{
	FEonformScalarField MakeGeologyField(const FEonformGridDomain& Domain, FName Name)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;

		FEonformScalarField Field;
		Field.Initialize(Domain, Descriptor);
		return Field;
	}
}

bool FEonformTerrainGeology::Build(
	const FEonformScalarField& Height,
	int32 Seed,
	const FEonformTerrainGeologySettings& Settings,
	FEonformTerrainDataset& InOutDataset,
	FString* OutError)
{
	auto Fail = [OutError](const TCHAR* Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (!Height.IsValid()) return Fail(TEXT("Geology requires a valid Height field."));

	const FEonformScalarField* Mountain = InOutDataset.FindScalarField(EonformTerrainFieldNames::Mountain);
	const FEonformScalarField* Plains = InOutDataset.FindScalarField(EonformTerrainFieldNames::Plains);
	const FEonformScalarField* Slope = InOutDataset.FindScalarField(EonformTerrainFieldNames::SlopeDegrees);
	const FEonformScalarField* Concavity = InOutDataset.FindScalarField(EonformTerrainFieldNames::Concavity);
	const FEonformScalarField* Required[] = { Mountain, Plains, Slope, Concavity };
	for (const FEonformScalarField* Field : Required)
	{
		if (!Field || !Field->IsValid() || Field->Domain != Height.Domain)
		{
			return Fail(TEXT("Geology requires Terrain Context fields on the same domain as Height."));
		}
	}

	FEonformFractalNoiseSettings NoiseSettings;
	NoiseSettings.Frequency = FMath::Max(Settings.Frequency, 0.000001f);
	NoiseSettings.Octaves = FMath::Clamp(Settings.Octaves, 1, 16);
	NoiseSettings.Persistence = 0.5f;
	NoiseSettings.Lacunarity = 2.0f;
	const FVector2D GeologyOffset = FEonformTerrainNoise::MakeSeedOffset(Seed, 707);

	FEonformScalarField RockHardness = MakeGeologyField(Height.Domain, EonformTerrainFieldNames::RockHardness);
	FEonformScalarField Weathering = MakeGeologyField(Height.Domain, EonformTerrainFieldNames::Weathering);
	FEonformScalarField SoilDepth = MakeGeologyField(Height.Domain, EonformTerrainFieldNames::SoilDepth);

	for (int32 Y = 0; Y < Height.Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Height.Domain.Dimensions.X; ++X)
		{
			const FVector2d World = Height.Domain.InteriorSampleToWorld(X, Y);
			float Lithology = FEonformTerrainNoise::SampleFractal(
				FVector2D(static_cast<float>(World.X), static_cast<float>(World.Y)),
				GeologyOffset,
				NoiseSettings);
			Lithology = FEonformTerrainShaping::ApplySignedPower(Lithology, Settings.Contrast);
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
