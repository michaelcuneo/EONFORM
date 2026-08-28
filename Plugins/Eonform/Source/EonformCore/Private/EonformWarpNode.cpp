#include "EonformWarpNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type = TEXT("Any"))
	{
		FEonformTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = Type;
		return P;
	}

	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = Default;
		P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max;
		return P;
	}

	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Integer; P.DefaultInteger = Default;
		P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max);
		return P;
	}

	FEonformTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Boolean; P.DefaultBoolean = Default;
		return P;
	}

	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Name; P.DefaultName = Default;
		for (const FName Option : Options) P.NameOptions.Add(Option);
		return P;
	}

	const FEonformTerrainValue* Input(const FEonformTerrainNodeInputs& Inputs, FName Name)
	{
		const FEonformTerrainValue* const* P = Inputs.Find(Name);
		return P ? *P : nullptr;
	}

	const FEonformScalarField* AsField(const FEonformTerrainValue* Value)
	{
		if (!Value || !Value->IsValid()) return nullptr;
		if (Value->Type == EEonformTerrainValueType::ScalarField) return &Value->ScalarField;
		if (Value->Type == EEonformTerrainValueType::Terrain) return Value->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
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
				Error = TEXT("Warp could not publish Height.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Prototype.HeightScale));
			return true;
		}
		Error = TEXT("Warp received an unsupported input type.");
		return false;
	}

	bool EvaluateWarpNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext&,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
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
		if (Modulator && Modulator->Domain != Source->Domain)
		{
			Error = TEXT("Warp modulator must share the Input domain.");
			return false;
		}

		EonformTerrainProceduralOps::FFractalWarpSettings Settings;
		Settings.Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.5)), 0.0001f, 4.0f);
		Settings.Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.5)), 0.0f, 5.0f);
		Settings.ZScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ZScale"), 0.0)), 0.0f, 4.0f);
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
		Settings.ModulationDirectionDegrees = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ModulationDirection"), 45.0)), -360.0f, 360.0f);
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		Settings.Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 1)), 1, 50);
		Settings.Mode = Node.GetName(TEXT("Mode"), TEXT("Vector Field"));

		FEonformScalarField Result;
		if (!EonformWarp::Apply(*Source, Settings, Result, &Error)) return false;
		return PublishLike(*SourceValue, MoveTemp(Result), Out, Error);
	}
}

bool EonformWarp::Apply(
	const FEonformScalarField& Source,
	const EonformTerrainProceduralOps::FFractalWarpSettings& Settings,
	FEonformScalarField& OutField,
	FString* OutError)
{
	return EonformTerrainProceduralOps::FractalWarpFidelity(Source, Settings, OutField, OutError);
}

void RegisterEonformAuthoritativeWarpNode()
{
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Warp;
	D.DisplayName = TEXT("Warp");
	D.Category = TEXT("Modify");
	D.Description = TEXT("Applies iterative fractal vector-field warping with optional modulation.");
	D.Inputs.Add(Port(TEXT("Input"), TEXT("Input")));
	D.Inputs.Add(Port(TEXT("Modulator"), TEXT("Modulator")));
	D.Outputs.Add(Port(TEXT("Out"), TEXT("Out")));
	D.Parameters = {
		Num(TEXT("Size"), TEXT("Size"), 0.5, 0.0001, 4.0),
		Num(TEXT("Strength"), TEXT("Strength"), 0.5, 0.0, 5.0),
		Num(TEXT("ZScale"), TEXT("Z Scale"), 0.0, 0.0, 4.0),
		Choice(TEXT("WarpSource"), TEXT("Warp Source"), TEXT("Perlin FBM"), { TEXT("Perlin FBM"), TEXT("Voronoi R"), TEXT("Voronoi P"), TEXT("Voronoi A"), TEXT("Voronoi S"), TEXT("Voronoi M"), TEXT("Voronoi D") }),
		Num(TEXT("Perturbation"), TEXT("Perturbation"), 0.5, 0.0, 1.0),
		Int(TEXT("Complexity"), TEXT("Complexity"), 12, 1, 12),
		Num(TEXT("Roughness"), TEXT("Roughness"), 0.4, 0.0, 1.0),
		Bool(TEXT("Normalized"), TEXT("Normalized"), false),
		Choice(TEXT("EdgeBehaviour"), TEXT("Edge Behaviour"), TEXT("Mirror"), { TEXT("Edge"), TEXT("Mirror") }),
		Num(TEXT("Modulation"), TEXT("Modulation"), 0.0, 0.0, 1.0),
		Num(TEXT("ModulationDirection"), TEXT("Modulation Direction"), 45.0, -360.0, 360.0),
		Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647),
		Int(TEXT("Iterations"), TEXT("Iterations"), 1, 1, 50),
		Choice(TEXT("Mode"), TEXT("Mode"), TEXT("Vector Field"), { TEXT("Bitmap"), TEXT("Vector Field"), TEXT("Vector Field Integral") })
	};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(D.Type, EvaluateWarpNode);
}
