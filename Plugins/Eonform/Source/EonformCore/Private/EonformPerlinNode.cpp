#include "EonformPerlinNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRawNoise.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor PerlinTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor PerlinNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor PerlinIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Integer;
		Parameter.DefaultInteger = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = static_cast<double>(Maximum);
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor PerlinNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FEonformGridDomain BuildPerlinDomain(const FEonformTerrainEvaluationContext& Context)
	{
		return Context.ResolveTargetDomain(
			FIntPoint(257, 257),
			FVector2d(-50000.0, -50000.0),
			FVector2d(50000.0, 50000.0));
	}

	FEonformGridDomain BuildPerlinReferenceDomain(const FEonformTerrainEvaluationContext& Context)
	{
		return Context.ResolveReferenceDomain(
			FIntPoint(257, 257),
			FVector2d(-50000.0, -50000.0),
			FVector2d(50000.0, 50000.0));
	}

	float ResolveHeightScale(const FEonformTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale())
		{
			return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		}
		return FMath::Max(Context.HeightScale, 1.0f);
	}

	bool EvaluatePerlinNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs&,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformGridDomain Domain = BuildPerlinDomain(Context);
		const FEonformGridDomain ReferenceDomain = BuildPerlinReferenceDomain(Context);
		if (!Domain.IsValid() || !ReferenceDomain.IsValid())
		{
			Error = TEXT("Perlin produced an invalid grid domain.");
			return false;
		}

		EonformTerrainProceduralOps::FPerlinSettings Settings;
		Settings.Type = Node.GetName(TEXT("Type"), TEXT("FBM"));
		Settings.Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.0001f, 1.0f);
		Settings.Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 10)), 1, 14);
		Settings.Gain = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Gain"), 0.5)), 0.0f, 1.0f);
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		Settings.WarpType = Node.GetName(TEXT("WarpType"), TEXT("Complex"));
		Settings.WarpFrequency = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpFrequency"), 0.05)), 0.0f, 1.0f);
		Settings.WarpAmplitude = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpAmplitude"), 0.5)), 0.0f, 1.0f);
		Settings.WarpOctaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("WarpOctaves"), 10)), 1, 14);
		Settings.ScaleX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), 1.0)), 0.0001f, 100.0f);
		Settings.ScaleY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), 1.0)), 0.0001f, 100.0f);
		Settings.X = 1.0f - FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("X"), 0.5)), 0.0f, 1.0f);
		Settings.Y = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Y"), 0.5)), 0.0f, 1.0f);

		FEonformScalarField HeightField;
		const float HeightAmount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), 0.0f, 1.0f);
		if (!EonformPerlin::Generate(Domain, Settings, HeightAmount, HeightField, &Error, &ReferenceDomain)) return false;

		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(HeightField)))
		{
			Error = TEXT("Perlin could not publish its Height field.");
			return false;
		}
		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), ResolveHeightScale(Context));
		if (!Result.IsValid())
		{
			Error = TEXT("Perlin produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

bool EonformPerlin::Generate(
	const FEonformGridDomain& Domain,
	const EonformTerrainProceduralOps::FPerlinSettings& Settings,
	float HeightAmount,
	FEonformScalarField& OutField,
	FString* OutError,
	const FEonformGridDomain* ReferenceDomain)
{
	if (!EonformTerrainRawNoise::Perlin(Domain, Settings, OutField, OutError, ReferenceDomain)) return false;
	const float Amount = FMath::Clamp(HeightAmount, 0.0f, 1.0f);
	for (float& Value : OutField.Values) Value *= Amount;
	if (OutError) OutError->Reset();
	return OutField.IsValid();
}

void RegisterEonformPerlinNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::PerlinNoise;
	Descriptor.DisplayName = TEXT("Perlin");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates Perlin terrain at the active graph resolution with warp and transform controls.");
	Descriptor.Outputs.Add(PerlinTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(PerlinNameParameter(TEXT("Type"), TEXT("Type"), TEXT("FBM"), { TEXT("FBM"), TEXT("Ridged"), TEXT("Billowy") }, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("Scale"), TEXT("Scale"), 0.5, 0.0, 1.0, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinIntegerParameter(TEXT("Octaves"), TEXT("Octaves"), 10, 1, 14, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("Gain"), TEXT("Gain"), 0.5, 0.0, 1.0, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("Height"), TEXT("Height"), 1.0, 0.0, 1.0, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Noise")));
	Descriptor.Parameters.Add(PerlinNameParameter(TEXT("WarpType"), TEXT("Type"), TEXT("Complex"), { TEXT("None"), TEXT("Simple"), TEXT("Complex") }, TEXT("Warp")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("WarpFrequency"), TEXT("Frequency"), 0.05, 0.0, 1.0, TEXT("Warp")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("WarpAmplitude"), TEXT("Amplitude"), 0.5, 0.0, 1.0, TEXT("Warp")));
	Descriptor.Parameters.Add(PerlinIntegerParameter(TEXT("WarpOctaves"), TEXT("Octaves"), 10, 1, 14, TEXT("Warp")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.0001, 100.0, TEXT("Transform")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.0001, 100.0, TEXT("Transform")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("X"), TEXT("X"), 0.5, 0.0, 1.0, TEXT("Transform")));
	Descriptor.Parameters.Add(PerlinNumberParameter(TEXT("Y"), TEXT("Y"), 0.5, 0.0, 1.0, TEXT("Transform")));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::PerlinNoise, EvaluatePerlinNode);
}
