#include "EonformTerrainFidelityNodes.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainFractalWarp.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainProceduralOps.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FEonformTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = Type;
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

	FEonformTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
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
		for (const FName V : Options) P.NameOptions.Add(V);
		if (Group) P.Group = Group;
		return P;
	}

	FEonformGridDomain BuildSourceDomain(const FEonformTerrainEvaluationContext& Context)
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
		return FEonformGridDomain::Make(FIntPoint(RX, RY), FVector2d(-W * 0.5, -H * 0.5), FVector2d(W * 0.5, H * 0.5));
	}

	float ResolveHeightScale(const FEonformTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale()) return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		return FMath::Max(Context.HeightScale, 1.0f);
	}

	const FEonformTerrainValue* Input(const FEonformTerrainNodeInputs& Inputs, FName Name)
	{
		const FEonformTerrainValue* const* P = Inputs.Find(Name);
		return P ? *P : nullptr;
	}

	const FEonformScalarField* AsField(const FEonformTerrainValue* V)
	{
		if (!V || !V->IsValid()) return nullptr;
		if (V->Type == EEonformTerrainValueType::ScalarField) return &V->ScalarField;
		if (V->Type == EEonformTerrainValueType::Terrain) return V->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		return nullptr;
	}

	bool PublishLike(const FEonformTerrainValue& Prototype, FEonformScalarField&& Field, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		if (Prototype.Type == EEonformTerrainValueType::ScalarField)
		{
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Field)));
			return true;
		}
		if (Prototype.Type == EEonformTerrainValueType::Terrain)
		{
			Field.Descriptor.Name = EonformTerrainFieldNames::Height;
			FEonformTerrainDataset Dataset = Prototype.TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(Field)))
			{
				Error = TEXT("Node could not publish Height.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Prototype.HeightScale));
			return true;
		}
		Error = TEXT("Unsupported input type.");
		return false;
	}

	bool EvaluateVoronoi(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs&, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformGridDomain Domain = BuildSourceDomain(Context);
		if (!Domain.IsValid())
		{
			Error = TEXT("Voronoi produced an invalid domain.");
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
		if (!EonformTerrainProceduralOps::GenerateVoronoi(Domain, Settings, Height, &Error)) return false;
		const float ClampV = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Clamp"), 1.0)), 0.0001f, 1.0f);
		for (float& Value : Height.Values) Value *= ClampV;

		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Voronoi could not publish Height.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), ResolveHeightScale(Context)));
		return true;
	}

	bool EvaluateWarp(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* SourceValue = Input(Inputs, TEXT("Input"));
		const FEonformScalarField* Source = AsField(SourceValue);
		if (!SourceValue || !Source)
		{
			Error = TEXT("Warp requires Input.");
			return false;
		}
		const FEonformScalarField* Modulator = AsField(Input(Inputs, TEXT("Modulator")));
		if (!Modulator) Modulator = AsField(Input(Inputs, TEXT("Modulation")));

		EonformTerrainProceduralOps::FFractalWarpSettings Settings;
		Settings.Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.5)), 0.0f, 1.0f);
		Settings.Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.5)), 0.0f, 1.0f);
		Settings.ZScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ZScale"), 0.0)), 0.0f, 1.0f);
		Settings.NoiseType = Node.GetName(TEXT("WarpSource"), TEXT("Perlin FBM"));
		Settings.Perturbation = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Perturbation"), 0.5)), 0.0f, 1.0f);
		Settings.Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Complexity"), 12)), 1, 12);
		Settings.Roughness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Roughness"), 0.4)), 0.0f, 1.0f);
		Settings.bNormalized = Node.GetBool(TEXT("Normalized"), false);
		Settings.EdgeBehaviour = Node.GetName(TEXT("EdgeBehaviour"), TEXT("Mirror")) == TEXT("Mirror")
			? EonformTerrainProceduralOps::EEdgeBehaviour::Mirror
			: EonformTerrainProceduralOps::EEdgeBehaviour::Edge;
		Settings.Modulator = Modulator;
		Settings.Modulation = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Modulation"), 0.0)), 0.0f, 1.0f);
		Settings.ModulationDirectionDegrees = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ModulationDirection"), 45.0)), 0.0f, 360.0f);
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		Settings.Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 1)), 1, 50);
		Settings.Mode = Node.GetName(TEXT("Mode"), TEXT("Vector Field"));

		FEonformScalarField Result;
		if (!EonformTerrainProceduralOps::FractalWarpFidelity(*Source, Settings, Result, &Error)) return false;
		return PublishLike(*SourceValue, MoveTemp(Result), Out, Error);
	}

	void RegisterVoronoi()
	{
		FEonformTerrainNodeDescriptor D;
		D.Type = EonformTerrainNodeTypes::Voronoi;
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
		FEonformTerrainNodeDescriptorRegistry::Register(D);
		FEonformTerrainNodeRegistry::Register(D.Type, EvaluateVoronoi);
	}

	void RegisterWarp()
	{
		FEonformTerrainNodeDescriptor D;
		D.Type = EonformTerrainNodeTypes::Warp;
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
		FEonformTerrainNodeDescriptorRegistry::Register(D);
		FEonformTerrainNodeRegistry::Register(D.Type, EvaluateWarp);
	}
}

void RegisterEonformTerrainFidelityNodes()
{
	RegisterVoronoi();
	RegisterWarp();
}
