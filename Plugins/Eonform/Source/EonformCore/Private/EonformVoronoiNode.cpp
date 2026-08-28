#include "EonformVoronoiNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRawNoise.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor Port(FName Name, const TCHAR* Label)
	{
		FEonformTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = TEXT("Terrain");
		return P;
	}

	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		if (Group) P.Group = Group;
		return P;
	}

	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = static_cast<double>(Max);
		if (Group) P.Group = Group;
		return P;
	}

	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName Option : Options) P.NameOptions.Add(Option);
		if (Group) P.Group = Group;
		return P;
	}

	FEonformGridDomain BuildDomain(const FEonformTerrainEvaluationContext& Context)
	{
		return Context.ResolveTargetDomain(
			FIntPoint(257, 257),
			FVector2d(-50000.0, -50000.0),
			FVector2d(50000.0, 50000.0));
	}

	FEonformGridDomain BuildReferenceDomain(const FEonformTerrainEvaluationContext& Context)
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

	bool EvaluateVoronoiNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs&,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformGridDomain Domain = BuildDomain(Context);
		const FEonformGridDomain ReferenceDomain = BuildReferenceDomain(Context);
		if (!Domain.IsValid() || !ReferenceDomain.IsValid())
		{
			Error = TEXT("Voronoi produced an invalid evaluation domain.");
			return false;
		}

		EonformTerrainProceduralOps::FVoronoiSettings Settings;
		Settings.Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.0001f, 1.0f);
		Settings.Jitter = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Jitter"), 0.5)), 0.0f, 1.0f);
		Settings.Function = Node.GetName(TEXT("Function"), TEXT("Euclidean"));
		Settings.Form = Node.GetName(TEXT("Form"), TEXT("P"));
		Settings.Gain = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Gain"), 0.5)), 0.0f, 1.0f);
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		Settings.WarpType = Node.GetName(TEXT("WarpType"), TEXT("Complex"));
		Settings.WarpFrequency = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpFrequency"), 0.05)), 0.0f, 1.0f);
		Settings.WarpAmplitude = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpAmplitude"), 0.5)), 0.0f, 1.0f);
		Settings.WarpOctaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("WarpOctaves"), 14)), 1, 14);
		Settings.ScaleX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), 1.0)), 0.0001f, 100.0f);
		Settings.ScaleY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), 1.0)), 0.0001f, 100.0f);
		Settings.X = 1.0f - FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("X"), 0.5)), 0.0f, 1.0f);
		Settings.Y = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Y"), 0.5)), 0.0f, 1.0f);

		FEonformScalarField Height;
		const float ClampValue = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Clamp"), 1.0)), 0.0001f, 1.0f);
		if (!EonformVoronoi::Generate(Domain, Settings, ClampValue, Height, &Error, &ReferenceDomain)) return false;

		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Voronoi could not publish Height.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), ResolveHeightScale(Context)));
		return true;
	}
}

bool EonformVoronoi::Generate(
	const FEonformGridDomain& Domain,
	const EonformTerrainProceduralOps::FVoronoiSettings& Settings,
	float ClampValue,
	FEonformScalarField& OutField,
	FString* OutError,
	const FEonformGridDomain* ReferenceDomain)
{
	if (!EonformTerrainRawNoise::Voronoi(Domain, Settings, OutField, OutError, ReferenceDomain)) return false;

	const float ClampedAmount = FMath::Clamp(ClampValue, 0.0001f, 1.0f);
	for (float& Value : OutField.Values)
	{
		Value *= ClampedAmount;
	}
	if (OutError) OutError->Reset();
	return OutField.IsValid();
}

void RegisterEonformVoronoiNode()
{
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Voronoi;
	D.DisplayName = TEXT("Voronoi");
	D.Category = TEXT("Primitive");
	D.Description = TEXT("Generates cellular terrain at the active graph resolution with form, warp and transform controls.");
	D.Outputs.Add(Port(TEXT("Out"), TEXT("Out")));
	D.Parameters = {
		Num(TEXT("Scale"), TEXT("Scale"), 0.5, 0.0001, 1.0, TEXT("Noise")),
		Num(TEXT("Jitter"), TEXT("Jitter"), 0.5, 0.0, 1.0, TEXT("Noise")),
		Choice(TEXT("Function"), TEXT("Function"), TEXT("Euclidean"), { TEXT("Euclidean"), TEXT("Manhattan") }, TEXT("Noise")),
		Choice(TEXT("Form"), TEXT("Form"), TEXT("P"), { TEXT("P"), TEXT("M"), TEXT("D"), TEXT("R"), TEXT("A"), TEXT("S"), TEXT("C"), TEXT("N") }, TEXT("Noise")),
		Num(TEXT("Gain"), TEXT("Gain"), 0.5, 0.0, 1.0, TEXT("Noise")),
		Num(TEXT("Clamp"), TEXT("Clamp"), 1.0, 0.0001, 1.0, TEXT("Noise")),
		Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Noise")),
		Choice(TEXT("WarpType"), TEXT("Type"), TEXT("Complex"), { TEXT("None"), TEXT("Simple"), TEXT("Complex") }, TEXT("Warp")),
		Num(TEXT("WarpFrequency"), TEXT("Frequency"), 0.05, 0.0, 1.0, TEXT("Warp")),
		Num(TEXT("WarpAmplitude"), TEXT("Amplitude"), 0.5, 0.0, 1.0, TEXT("Warp")),
		Int(TEXT("WarpOctaves"), TEXT("Octaves"), 14, 1, 14, TEXT("Warp")),
		Num(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.0001, 100.0, TEXT("Transform")),
		Num(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.0001, 100.0, TEXT("Transform")),
		Num(TEXT("X"), TEXT("X"), 0.5, 0.0, 1.0, TEXT("Transform")),
		Num(TEXT("Y"), TEXT("Y"), 0.5, 0.0, 1.0, TEXT("Transform"))
	};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(D.Type, EvaluateVoronoiNode);
}
