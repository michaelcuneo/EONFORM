#include "GaeaTerrainFidelityNodes.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainProceduralOps.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = Type;
		return P;
	}

	FGaeaTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = static_cast<double>(Max);
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName V : Options) P.NameOptions.Add(V);
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaGridDomain BuildSourceDomain(const FGaeaTerrainEvaluationContext& Context)
	{
		const int32 RX = FMath::Clamp(Context.TargetResolution.X > 1 ? Context.TargetResolution.X : 257, 2, 4097);
		const int32 RY = FMath::Clamp(Context.TargetResolution.Y > 1 ? Context.TargetResolution.Y : RX, 2, 4097);
		double W = 100000.0;
		double H = 100000.0;
		if (Context.PhysicalMetrics.HasWorldDimensions())
		{
			W = Context.PhysicalMetrics.WorldWidthMeters * 100.0;
			H = Context.PhysicalMetrics.WorldDepthMeters * 100.0;
		}
		return FGaeaGridDomain::Make(FIntPoint(RX, RY), FVector2d(-W * 0.5, -H * 0.5), FVector2d(W * 0.5, H * 0.5));
	}

	float ResolveHeightScale(const FGaeaTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale()) return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		return FMath::Max(Context.HeightScale, 1.0f);
	}

	const FGaeaTerrainValue* Input(const FGaeaTerrainNodeInputs& Inputs, FName Name)
	{
		const FGaeaTerrainValue* const* P = Inputs.Find(Name);
		return P ? *P : nullptr;
	}

	const FGaeaScalarField* AsField(const FGaeaTerrainValue* V)
	{
		if (!V || !V->IsValid()) return nullptr;
		if (V->Type == EGaeaTerrainValueType::ScalarField) return &V->ScalarField;
		if (V->Type == EGaeaTerrainValueType::Terrain) return V->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		return nullptr;
	}

	bool PublishLike(const FGaeaTerrainValue& Prototype, FGaeaScalarField&& Field, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		if (Prototype.Type == EGaeaTerrainValueType::ScalarField)
		{
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Field)));
			return true;
		}
		if (Prototype.Type == EGaeaTerrainValueType::Terrain)
		{
			Field.Descriptor.Name = GaeaTerrainFieldNames::Height;
			FGaeaTerrainDataset Dataset = Prototype.TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(Field)))
			{
				Error = TEXT("Node could not publish Height.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Prototype.HeightScale));
			return true;
		}
		Error = TEXT("Unsupported input type.");
		return false;
	}

	bool EvaluateVoronoi(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaGridDomain Domain = BuildSourceDomain(Context);
		if (!Domain.IsValid())
		{
			Error = TEXT("Voronoi produced an invalid domain.");
			return false;
		}

		GaeaTerrainProceduralOps::FVoronoiSettings Settings;
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

		FGaeaScalarField Height;
		if (!GaeaTerrainProceduralOps::GenerateVoronoi(Domain, Settings, Height, &Error)) return false;
		const float ClampV = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Clamp"), 1.0)), 0.0001f, 1.0f);
		for (float& Value : Height.Values) Value *= ClampV;

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Voronoi could not publish Height.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), ResolveHeightScale(Context)));
		return true;
	}

	bool EvaluateDirectionalWarp(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* SourceValue = Input(Inputs, TEXT("Input"));
		const FGaeaScalarField* Source = AsField(SourceValue);
		const FGaeaScalarField* Custom = AsField(Input(Inputs, TEXT("Custom")));
		if (!Custom) Custom = AsField(Input(Inputs, TEXT("Guide")));
		if (!SourceValue || !Source)
		{
			Error = TEXT("DirectionalWarp requires Input.");
			return false;
		}
		if (!Custom || Custom->Domain != Source->Domain)
		{
			Error = TEXT("DirectionalWarp requires a matching Custom guide field.");
			return false;
		}

		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.25)), 0.0f, 5.0f);
		const float Direction = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Direction"), 45.0)), 0.0f, 360.0f);
		const FName Edge = Node.GetName(TEXT("EdgeBehaviour"), TEXT("Mirror"));
		FGaeaScalarField Result;
		if (!GaeaTerrainProceduralOps::DirectionWarpNormalized(
			*Source,
			*Custom,
			Strength,
			Direction,
			Edge == TEXT("Mirror") ? GaeaTerrainProceduralOps::EEdgeBehaviour::Mirror : GaeaTerrainProceduralOps::EEdgeBehaviour::Edge,
			Result,
			&Error)) return false;
		return PublishLike(*SourceValue, MoveTemp(Result), Out, Error);
	}

	bool EvaluateWarp(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* SourceValue = Input(Inputs, TEXT("Input"));
		const FGaeaScalarField* Source = AsField(SourceValue);
		if (!SourceValue || !Source)
		{
			Error = TEXT("Warp requires Input.");
			return false;
		}
		const FGaeaScalarField* Modulator = AsField(Input(Inputs, TEXT("Modulator")));
		if (!Modulator) Modulator = AsField(Input(Inputs, TEXT("Modulation")));

		GaeaTerrainProceduralOps::FFractalWarpSettings Settings;
		Settings.Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.5)), 0.0f, 1.0f);
		Settings.Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.5)), 0.0f, 1.0f);
		Settings.ZScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ZScale"), 0.0)), 0.0f, 1.0f);
		Settings.NoiseType = Node.GetName(TEXT("WarpSource"), TEXT("Perlin FBM"));
		Settings.Perturbation = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Perturbation"), 0.5)), 0.0f, 1.0f);
		Settings.Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Complexity"), 12)), 1, 12);
		Settings.Roughness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Roughness"), 0.4)), 0.0f, 1.0f);
		Settings.bNormalized = Node.GetBool(TEXT("Normalized"), false);
		Settings.EdgeBehaviour = Node.GetName(TEXT("EdgeBehaviour"), TEXT("Mirror")) == TEXT("Mirror")
			? GaeaTerrainProceduralOps::EEdgeBehaviour::Mirror
			: GaeaTerrainProceduralOps::EEdgeBehaviour::Edge;
		Settings.Modulator = Modulator;
		Settings.Modulation = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Modulation"), 0.0)), 0.0f, 1.0f);
		Settings.ModulationDirectionDegrees = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ModulationDirection"), 45.0)), 0.0f, 360.0f);
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		Settings.Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 1)), 1, 50);
		Settings.Mode = Node.GetName(TEXT("Mode"), TEXT("Vector Field"));

		FGaeaScalarField Result;
		if (!GaeaTerrainProceduralOps::FractalWarp(*Source, Settings, Result, &Error)) return false;
		return PublishLike(*SourceValue, MoveTemp(Result), Out, Error);
	}

	void RegisterVoronoi()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::Voronoi;
		D.DisplayName = TEXT("Voronoi");
		D.Category = TEXT("Primitive");
		D.Description = TEXT("Generates cellular terrain at the active graph resolution with documented form, warp and transform controls.");
		D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain")));
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
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateVoronoi);
	}

	void RegisterDirectionalWarp()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::DirectionalWarp;
		D.DisplayName = TEXT("DirectionalWarp");
		D.Category = TEXT("Modify");
		D.Description = TEXT("Warps Input along a fixed direction using a Custom scalar guide centered at 0.5.");
		D.Inputs.Add(Port(TEXT("Input"), TEXT("Input"), TEXT("Any")));
		D.Inputs.Add(Port(TEXT("Custom"), TEXT("Custom"), TEXT("Any")));
		D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Any")));
		D.Parameters = {
			Num(TEXT("Strength"), TEXT("Strength"), 0.25, 0.0, 5.0),
			Num(TEXT("Direction"), TEXT("Direction"), 45.0, 0.0, 360.0),
			Choice(TEXT("EdgeBehaviour"), TEXT("Edge Behaviour"), TEXT("Mirror"), { TEXT("Edge"), TEXT("Mirror") })
		};
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateDirectionalWarp);
	}

	void RegisterWarp()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::Warp;
		D.DisplayName = TEXT("Warp");
		D.Category = TEXT("Modify");
		D.Description = TEXT("Applies iterative fractal vector-field warping with optional modulation.");
		D.Inputs.Add(Port(TEXT("Input"), TEXT("Input"), TEXT("Any")));
		D.Inputs.Add(Port(TEXT("Modulator"), TEXT("Modulator"), TEXT("Any")));
		D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Any")));
		D.Parameters = {
			Num(TEXT("Size"), TEXT("Size"), 0.5, 0.0, 1.0),
			Num(TEXT("Strength"), TEXT("Strength"), 0.5, 0.0, 1.0),
			Num(TEXT("ZScale"), TEXT("Z Scale"), 0.0, 0.0, 1.0),
			Choice(TEXT("WarpSource"), TEXT("Warp Source"), TEXT("Perlin FBM"), { TEXT("Perlin FBM"), TEXT("Voronoi R"), TEXT("Voronoi P"), TEXT("Voronoi A"), TEXT("Voronoi S"), TEXT("Voronoi M"), TEXT("Voronoi D") }),
			Num(TEXT("Perturbation"), TEXT("Perturbation"), 0.5, 0.0, 1.0),
			Int(TEXT("Complexity"), TEXT("Complexity"), 12, 1, 12),
			Num(TEXT("Roughness"), TEXT("Roughness"), 0.4, 0.0, 1.0),
			Bool(TEXT("Normalized"), TEXT("Normalized"), false),
			Choice(TEXT("EdgeBehaviour"), TEXT("Edge Behaviour"), TEXT("Mirror"), { TEXT("Edge"), TEXT("Mirror") }),
			Num(TEXT("Modulation"), TEXT("Modulation"), 0.0, 0.0, 1.0),
			Num(TEXT("ModulationDirection"), TEXT("Modulation Direction"), 45.0, 0.0, 360.0),
			Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647),
			Int(TEXT("Iterations"), TEXT("Iterations"), 1, 1, 50),
			Choice(TEXT("Mode"), TEXT("Mode"), TEXT("Vector Field"), { TEXT("Bitmap"), TEXT("Vector Field"), TEXT("Vector Field Integral") })
		};
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateWarp);
	}
}

void RegisterGaeaTerrainFidelityNodes()
{
	RegisterVoronoi();
	RegisterDirectionalWarp();
	RegisterWarp();
}
