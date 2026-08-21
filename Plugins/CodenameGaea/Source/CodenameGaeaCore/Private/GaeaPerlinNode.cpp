#include "GaeaPerlinNode.h"

#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNoise.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor PerlinTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor PerlinNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor PerlinIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Integer;
		Parameter.DefaultInteger = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = static_cast<double>(Maximum);
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor PerlinNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	float PerlinShape(float Value, FName Type)
	{
		if (Type == TEXT("Ridged")) return FMath::Clamp(1.0f - FMath::Abs(Value), -1.0f, 1.0f);
		if (Type == TEXT("Billowy")) return FMath::Clamp(FMath::Abs(Value) * 2.0f - 1.0f, -1.0f, 1.0f);
		return FMath::Clamp(Value, -1.0f, 1.0f);
	}

	bool EvaluatePerlinNodeCurrent(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		// Build-domain controls are engine integration details, not Gaea node
		// properties. Existing recipes may still carry them, so keep honoring
		// them without exposing them on the public node.
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);

		const FName Type = Node.GetName(TEXT("Type"), TEXT("FBM"));
		if (Type != TEXT("FBM") && Type != TEXT("Ridged") && Type != TEXT("Billowy"))
		{
			Error = TEXT("Perlin Type must be FBM, Ridged, or Billowy.");
			return false;
		}
		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.001f, 10.0f);
		const int32 Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 6)), 1, 16);
		const float Gain = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Gain"), 0.5)), 0.0f, 1.0f);
		const float HeightAmount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), 0.0f, 4.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const FName WarpType = Node.GetName(TEXT("WarpType"), TEXT("None"));
		const float WarpFrequency = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpFrequency"), 0.5)), 0.001f, 10.0f);
		const float WarpAmplitude = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpAmplitude"), 0.0)), 0.0f, 4.0f);
		const int32 WarpOctaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("WarpOctaves"), 2)), 1, 8);
		const float ScaleX = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), 1.0)), 0.001f);
		const float ScaleY = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), 1.0)), 0.001f);
		const float OffsetX = static_cast<float>(Node.GetNumber(TEXT("X"), 0.0));
		const float OffsetY = static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0));

		const double HalfWorldSize = static_cast<double>(WorldSize) * 0.5;
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(FIntPoint(Resolution, Resolution), FVector2d(-HalfWorldSize, -HalfWorldSize), FVector2d(HalfWorldSize, HalfWorldSize));
		if (!Domain.IsValid())
		{
			Error = TEXT("Perlin produced an invalid grid domain.");
			return false;
		}

		FGaeaFractalNoiseSettings Settings;
		Settings.Frequency = FMath::Clamp(0.00055f / Scale, 0.000001f, 1.0f);
		Settings.Octaves = Octaves;
		Settings.Persistence = Gain;
		Settings.Lacunarity = 2.0f;
		FGaeaFractalNoiseSettings WarpSettings = Settings;
		WarpSettings.Frequency = FMath::Clamp(Settings.Frequency * WarpFrequency, 0.000001f, 1.0f);
		WarpSettings.Octaves = WarpOctaves;

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField HeightField;
		HeightField.Initialize(Domain, Descriptor);
		const FVector2D SeedOffset = FGaeaTerrainNoise::MakeSeedOffset(Seed);
		const FVector2D WarpSeedA = FGaeaTerrainNoise::MakeSeedOffset(Seed ^ 0x51ed270b);
		const FVector2D WarpSeedB = FGaeaTerrainNoise::MakeSeedOffset(Seed ^ 0x68bc21eb);

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const FVector2d World = Domain.InteriorSampleToWorld(X, Y);
				FVector2D P((static_cast<float>(World.X) + OffsetX) / ScaleX, (static_cast<float>(World.Y) + OffsetY) / ScaleY);
				if (WarpType != TEXT("None") && WarpAmplitude > 0.0f)
				{
					const float WX = FGaeaTerrainNoise::SampleFractal(P, WarpSeedA, WarpSettings);
					const float WY = FGaeaTerrainNoise::SampleFractal(P, WarpSeedB, WarpSettings);
					const float Complexity = WarpType == TEXT("Complex") ? 2.0f : 1.0f;
					P += FVector2D(WX, WY) * WarpAmplitude * WorldSize * 0.02f * Complexity;
				}
				const float Raw = FGaeaTerrainNoise::SampleFractal(P, SeedOffset, Settings);
				HeightField.AtInterior(X, Y) = FMath::Clamp(PerlinShape(Raw, Type) * HeightAmount, -1.0f, 1.0f);
			}
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(HeightField)))
		{
			Error = TEXT("Perlin could not publish its Height field.");
			return false;
		}
		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Perlin produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaPerlinNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Descriptor.DisplayName = TEXT("Perlin");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates Perlin-based FBM, ridged, or billowy terrain with optional domain warp and transform controls.");
	Descriptor.Outputs.Add(PerlinTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(PerlinNameParameter(TEXT("Type"), TEXT("Type"), TEXT("FBM"), { TEXT("FBM"), TEXT("Ridged"), TEXT("Billowy") }, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("Scale"), TEXT("Scale"), 0.5, 0.001, 10.0, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinIntegerParameter(TEXT("Octaves"), TEXT("Octaves"), 6, 1, 16, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("Gain"), TEXT("Gain"), 0.5, 0.0, 1.0, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("Height"), TEXT("Height"), 1.0, 0.0, 4.0, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinNameParameter(TEXT("WarpType"), TEXT("Type"), TEXT("None"), { TEXT("None"), TEXT("Simple"), TEXT("Complex") }, TEXT("Warp")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("WarpFrequency"), TEXT("Frequency"), 0.5, 0.001, 10.0, TEXT("Warp")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("WarpAmplitude"), TEXT("Amplitude"), 0.0, 0.0, 4.0, TEXT("Warp")));
	Descriptor.Parameters.Add(PerlinIntegerParameter(TEXT("WarpOctaves"), TEXT("Octaves"), 2, 1, 8, TEXT("Warp")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.001, 10.0, TEXT("Transform")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.001, 10.0, TEXT("Transform")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("X"), TEXT("X"), 0.0, -1000000.0, 1000000.0, TEXT("Transform")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("Y"), TEXT("Y"), 0.0, -1000000.0, 1000000.0, TEXT("Transform")));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::PerlinNoise, EvaluatePerlinNodeCurrent);
}
