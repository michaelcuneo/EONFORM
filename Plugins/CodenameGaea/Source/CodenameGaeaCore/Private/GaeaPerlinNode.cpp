#include "GaeaPerlinNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainProceduralOps.h"
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

	FGaeaGridDomain BuildPerlinDomain(const FGaeaTerrainEvaluationContext& Context)
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

	float ResolveHeightScale(const FGaeaTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale())
		{
			return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		}
		return FMath::Max(Context.HeightScale, 1.0f);
	}

	bool EvaluatePerlinNodeCurrent(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaGridDomain Domain = BuildPerlinDomain(Context);
		if (!Domain.IsValid())
		{
			Error = TEXT("Perlin produced an invalid grid domain.");
			return false;
		}

		GaeaTerrainProceduralOps::FPerlinSettings Settings;
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

		FGaeaScalarField HeightField;
		if (!GaeaTerrainProceduralOps::GeneratePerlin(Domain, Settings, HeightField, &Error)) return false;

		const float HeightAmount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), 0.0f, 1.0f);
		for (float& Value : HeightField.Values) Value *= HeightAmount;

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(HeightField)))
		{
			Error = TEXT("Perlin could not publish its Height field.");
			return false;
		}
		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), ResolveHeightScale(Context));
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
	Descriptor.Description = TEXT("Generates Perlin FBM terrain at the active graph resolution with the documented warp and transform controls.");
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
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::PerlinNoise, EvaluatePerlinNodeCurrent);
}
