#include "EonformTerrainShaping.h"

#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNoise.h"

namespace
{
	FEonformScalarField MakeNormalizedField(const FEonformGridDomain& Domain, FName Name)
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

float FEonformTerrainShaping::BuildMountainMask(float MacroNoise, float Threshold, float TransitionWidth)
{
	const float SafeWidth = FMath::Max(TransitionWidth, UE_SMALL_NUMBER);
	const float Lower = Threshold - SafeWidth * 0.5f;
	const float Upper = Threshold + SafeWidth * 0.5f;
	const float T = FMath::Clamp((MacroNoise - Lower) / (Upper - Lower), 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

float FEonformTerrainShaping::BuildFoothillMask(float MountainMask, float Width)
{
	const float SafeWidth = FMath::Clamp(Width, 0.01f, 1.0f);
	const float DistanceFromEdge = FMath::Abs(MountainMask - 0.5f) * 2.0f;
	const float T = FMath::Clamp(1.0f - DistanceFromEdge / SafeWidth, 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

float FEonformTerrainShaping::BuildValleyMask(float ValleyNoise, float Width, float Sharpness)
{
	const float SafeWidth = FMath::Max(Width, 0.001f);
	const float SafeSharpness = FMath::Max(Sharpness, 0.01f);
	const float DistanceToCenter = FMath::Abs(ValleyNoise);
	const float T = FMath::Clamp(1.0f - DistanceToCenter / SafeWidth, 0.0f, 1.0f);
	return FMath::Pow(T, SafeSharpness);
}

float FEonformTerrainShaping::ApplySignedPower(float Value, float Exponent)
{
	const float SafeExponent = FMath::Max(Exponent, UE_SMALL_NUMBER);
	return FMath::Sign(Value) * FMath::Pow(FMath::Abs(Value), SafeExponent);
}

bool FEonformTerrainShaping::Apply(
	const FEonformScalarField& InputHeight,
	const FEonformTerrainShapeSettings& Settings,
	FEonformTerrainDataset& InOutDataset,
	FString* OutError)
{
	auto Fail = [OutError](const TCHAR* Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (!InputHeight.IsValid())
	{
		return Fail(TEXT("Terrain Shape requires a valid Height field."));
	}

	FEonformFractalNoiseSettings MacroSettings;
	MacroSettings.Frequency = FMath::Max(Settings.MacroFrequency, 0.000001f);
	MacroSettings.Octaves = FMath::Clamp(Settings.MacroOctaves, 1, 16);
	MacroSettings.Persistence = 0.5f;
	MacroSettings.Lacunarity = 2.0f;

	FEonformFractalNoiseSettings WarpSettings;
	WarpSettings.Frequency = FMath::Max(Settings.WarpFrequency, 0.000001f);
	WarpSettings.Octaves = 3;
	WarpSettings.Persistence = 0.5f;
	WarpSettings.Lacunarity = 2.0f;

	FEonformFractalNoiseSettings RidgeSettings;
	RidgeSettings.Frequency = FMath::Max(Settings.RidgeFrequency, 0.000001f);
	RidgeSettings.Octaves = FMath::Clamp(Settings.RidgeOctaves, 1, 16);
	RidgeSettings.Persistence = 0.5f;
	RidgeSettings.Lacunarity = 2.0f;

	FEonformFractalNoiseSettings FoothillSettings;
	FoothillSettings.Frequency = FMath::Max(Settings.FoothillFrequency, 0.000001f);
	FoothillSettings.Octaves = 3;
	FoothillSettings.Persistence = 0.5f;
	FoothillSettings.Lacunarity = 2.0f;

	FEonformFractalNoiseSettings ValleySettings;
	ValleySettings.Frequency = FMath::Max(Settings.ValleyFrequency, 0.000001f);
	ValleySettings.Octaves = 2;
	ValleySettings.Persistence = 0.5f;
	ValleySettings.Lacunarity = 2.0f;

	FEonformFractalNoiseSettings PlainsSettings;
	PlainsSettings.Frequency = FMath::Max(Settings.PlainsRollingFrequency, 0.000001f);
	PlainsSettings.Octaves = 3;
	PlainsSettings.Persistence = 0.5f;
	PlainsSettings.Lacunarity = 2.0f;

	const FVector2D MacroOffset = FEonformTerrainNoise::MakeSeedOffset(Settings.Seed, 17);
	const FVector2D WarpXOffset = FEonformTerrainNoise::MakeSeedOffset(Settings.Seed, 101);
	const FVector2D WarpYOffset = FEonformTerrainNoise::MakeSeedOffset(Settings.Seed, 202);
	const FVector2D RidgeOffset = FEonformTerrainNoise::MakeSeedOffset(Settings.Seed, 303);
	const FVector2D FoothillOffset = FEonformTerrainNoise::MakeSeedOffset(Settings.Seed, 404);
	const FVector2D ValleyOffset = FEonformTerrainNoise::MakeSeedOffset(Settings.Seed, 505);
	const FVector2D PlainsOffset = FEonformTerrainNoise::MakeSeedOffset(Settings.Seed, 606);

	FEonformScalarField Height = InputHeight;
	Height.Descriptor.Name = EonformTerrainFieldNames::Height;
	FEonformScalarField Mountain = MakeNormalizedField(InputHeight.Domain, EonformTerrainFieldNames::Mountain);
	FEonformScalarField Foothill = MakeNormalizedField(InputHeight.Domain, EonformTerrainFieldNames::Foothill);
	FEonformScalarField Plains = MakeNormalizedField(InputHeight.Domain, EonformTerrainFieldNames::Plains);

	const FIntPoint Dimensions = InputHeight.Domain.Dimensions;
	for (int32 Y = 0; Y < Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Dimensions.X; ++X)
		{
			const FVector2d WorldD = InputHeight.Domain.InteriorSampleToWorld(X, Y);
			const FVector2D World(static_cast<float>(WorldD.X), static_cast<float>(WorldD.Y));

			float PreliminaryMacro = 0.0f;
			float PreliminaryMountain = Settings.bEnableMountainMask ? 0.0f : 1.0f;
			float PreliminaryFoothill = 0.0f;

			if (Settings.bEnableMacroShape)
			{
				PreliminaryMacro = FEonformTerrainNoise::SampleFractal(World, MacroOffset, MacroSettings);
				PreliminaryMacro = ApplySignedPower(PreliminaryMacro, Settings.MacroContrast);
				if (Settings.bEnableMountainMask)
				{
					PreliminaryMountain = BuildMountainMask(PreliminaryMacro, Settings.MountainThreshold, Settings.MountainTransition);
				}
			}

			if (Settings.bEnableFoothills)
			{
				PreliminaryFoothill = BuildFoothillMask(PreliminaryMountain, Settings.FoothillWidth);
			}

			FVector2D SamplePosition = World;
			if (Settings.bEnableDomainWarp && Settings.WarpStrength > 0.0f)
			{
				const float WarpX = FEonformTerrainNoise::SampleFractal(World, WarpXOffset, WarpSettings);
				const float WarpY = FEonformTerrainNoise::SampleFractal(World, WarpYOffset, WarpSettings);
				const float NaturalWarpRegion = FMath::Clamp(PreliminaryMountain + PreliminaryFoothill * 0.7f + 0.08f, 0.0f, 1.0f);
				const float WarpMask = FMath::Lerp(1.0f, NaturalWarpRegion, FMath::Clamp(Settings.WarpRegionality, 0.0f, 1.0f));
				SamplePosition += FVector2D(WarpX, WarpY) * (Settings.WarpStrength * WarpMask);
			}

			const float BaseHeight = InputHeight.SampleWorld(FVector2d(SamplePosition.X, SamplePosition.Y), true);
			float MacroHeight = PreliminaryMacro;
			float MountainMask = PreliminaryMountain;
			if (Settings.bEnableMacroShape)
			{
				MacroHeight = FEonformTerrainNoise::SampleFractal(SamplePosition, MacroOffset, MacroSettings);
				MacroHeight = ApplySignedPower(MacroHeight, Settings.MacroContrast);
				if (Settings.bEnableMountainMask)
				{
					MountainMask = BuildMountainMask(MacroHeight, Settings.MountainThreshold, Settings.MountainTransition);
				}
			}

			const float FoothillMask = Settings.bEnableFoothills
				? BuildFoothillMask(MountainMask, Settings.FoothillWidth)
				: 0.0f;
			const float PlainsMask = FMath::Clamp((1.0f - MountainMask) * (1.0f - FoothillMask * 0.65f), 0.0f, 1.0f);

			float ShapedHeight = BaseHeight * FMath::Max(Settings.BaseStrength, 0.0f);
			if (Settings.bEnableMacroShape)
			{
				ShapedHeight += MacroHeight * Settings.MacroStrength;
			}

			if (Settings.bEnableRidges && Settings.RidgeStrength > 0.0f)
			{
				const float Ridge = FEonformTerrainNoise::SampleRidged(SamplePosition, RidgeOffset, RidgeSettings, Settings.RidgeSharpness);
				ShapedHeight += (Ridge * 2.0f - 1.0f) * Settings.RidgeStrength * MountainMask;
			}

			if (Settings.bEnableFoothills && Settings.FoothillStrength > 0.0f)
			{
				const float FoothillNoise = FEonformTerrainNoise::SampleFractal(SamplePosition, FoothillOffset, FoothillSettings);
				ShapedHeight += FoothillNoise * Settings.FoothillStrength * FoothillMask;
			}

			if (Settings.bEnableValleys && Settings.ValleyDepth > 0.0f)
			{
				const float ValleyNoise = FEonformTerrainNoise::SampleFractal(SamplePosition, ValleyOffset, ValleySettings);
				const float ValleyMask = BuildValleyMask(ValleyNoise, Settings.ValleyWidth, Settings.ValleySharpness);
				const float ValleyLandMask = FMath::Clamp(PlainsMask + FoothillMask * 0.45f, 0.0f, 1.0f);
				ShapedHeight -= ValleyMask * Settings.ValleyDepth * ValleyLandMask;
			}

			if (Settings.bEnablePlains && Settings.PlainsStrength > 0.0f)
			{
				const float FlattenedHeight = ApplySignedPower(ShapedHeight, Settings.PlainsFlattenExponent);
				const float RollingNoise = FEonformTerrainNoise::SampleFractal(SamplePosition, PlainsOffset, PlainsSettings);
				const float PlainsTarget = FlattenedHeight + RollingNoise * Settings.PlainsRollingStrength;
				ShapedHeight = FMath::Lerp(ShapedHeight, PlainsTarget, PlainsMask * Settings.PlainsStrength);
			}

			Height.AtInterior(X, Y) = FMath::Clamp(ShapedHeight, -1.0f, 1.0f);
			Mountain.AtInterior(X, Y) = MountainMask;
			Foothill.AtInterior(X, Y) = FoothillMask;
			Plains.AtInterior(X, Y) = PlainsMask;
		}
	}

	if (!InOutDataset.SetScalarField(MoveTemp(Height))
		|| !InOutDataset.SetScalarField(MoveTemp(Mountain))
		|| !InOutDataset.SetScalarField(MoveTemp(Foothill))
		|| !InOutDataset.SetScalarField(MoveTemp(Plains)))
	{
		return Fail(TEXT("Terrain Shape could not publish its shaped fields."));
	}

	if (OutError) OutError->Reset();
	return true;
}
