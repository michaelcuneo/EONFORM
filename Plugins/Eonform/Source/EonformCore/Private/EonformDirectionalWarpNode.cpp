#include "EonformDirectionalWarpNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	constexpr float DirectionDegreesToRadians = 0.017453292f;

	FEonformTerrainPortDescriptor Port(FName Name, const TCHAR* Label)
	{
		FEonformTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = TEXT("Any");
		return P;
	}

	FEonformTerrainParameterDescriptor Number(FName Name, const TCHAR* Label, double Default, double Min, double Max)
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
		return P;
	}

	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Name;
		P.DefaultName = Default;
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
				Error = TEXT("DirectionalWarp could not publish Height.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Prototype.HeightScale));
			return true;
		}
		Error = TEXT("DirectionalWarp received an unsupported input type.");
		return false;
	}

	int32 MirrorIndex(int32 Index, int32 Resolution)
	{
		if (Resolution <= 0) return 0;
		Index = FMath::Abs(Index);
		const int32 Period = Resolution * 2;
		Index %= Period;
		return Index < Resolution ? Index : (Period - 1 - Index);
	}

	float Bilinear(const FEonformScalarField& Field, float X, float Y, EonformDirectionalWarp::EEdgeBehaviour Edge)
	{
		const int32 W = Field.Domain.Dimensions.X;
		const int32 H = Field.Domain.Dimensions.Y;

		int32 X0 = static_cast<int32>(X);
		int32 Y0 = static_cast<int32>(Y);
		int32 X1 = X0 + 1;
		int32 Y1 = Y0 + 1;
		const float TX = X - static_cast<float>(X0);
		const float TY = Y - static_cast<float>(Y0);

		if (Edge == EonformDirectionalWarp::EEdgeBehaviour::Edge)
		{
			X0 = FMath::Clamp(X0, 0, W - 1);
			Y0 = FMath::Clamp(Y0, 0, H - 1);
			X1 = FMath::Clamp(X1, 0, W - 1);
			Y1 = FMath::Clamp(Y1, 0, H - 1);
		}
		else
		{
			X0 = MirrorIndex(X0, W);
			X1 = MirrorIndex(X1, W);
			Y0 = MirrorIndex(Y0, H);
			Y1 = MirrorIndex(Y1, H);
		}

		const float Top = Field.AtInterior(X0, Y0) * (1.0f - TX) + Field.AtInterior(X1, Y0) * TX;
		const float Bottom = Field.AtInterior(X0, Y1) * (1.0f - TX) + Field.AtInterior(X1, Y1) * TX;
		return Top * (1.0f - TY) + Bottom * TY;
	}

	bool EvaluateDirectionalWarp(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext&,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* SourceValue = Input(Inputs, TEXT("Input"));
		const FEonformScalarField* Source = AsField(SourceValue);
		const FEonformScalarField* Custom = AsField(Input(Inputs, TEXT("Custom")));
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
		FEonformScalarField Result;
		if (!EonformDirectionalWarp::ApplyNormalized(
			*Source,
			*Custom,
			Strength,
			Direction,
			Edge == TEXT("Mirror") ? EonformDirectionalWarp::EEdgeBehaviour::Mirror : EonformDirectionalWarp::EEdgeBehaviour::Edge,
			Result,
			&Error)) return false;
		return PublishLike(*SourceValue, MoveTemp(Result), Out, Error);
	}
}

bool EonformDirectionalWarp::ApplyPixels(
	const FEonformScalarField& Source,
	const FEonformScalarField& Custom,
	float StrengthPixels,
	float DirectionDegrees,
	EEdgeBehaviour EdgeBehaviour,
	FEonformScalarField& OutField,
	FString* OutError)
{
	if (!Source.IsValid() || !Custom.IsValid() || Source.Domain != Custom.Domain)
	{
		if (OutError) *OutError = TEXT("Directional warp requires matching valid source/custom fields.");
		return false;
	}

	const float Radians = DirectionDegrees * DirectionDegreesToRadians;
	const FVector2D Direction(-FMath::Cos(Radians) * StrengthPixels, FMath::Sin(Radians) * StrengthPixels);
	OutField = Source;
	for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
		{
			const FVector2D Offset = Direction * (Custom.AtInterior(X, Y) - 0.5f);
			OutField.AtInterior(X, Y) = Bilinear(Source, X + Offset.X, Y + Offset.Y, EdgeBehaviour);
		}
	}
	if (OutError) OutError->Reset();
	return OutField.IsValid();
}

bool EonformDirectionalWarp::ApplyNormalized(
	const FEonformScalarField& Source,
	const FEonformScalarField& Custom,
	float Strength,
	float DirectionDegrees,
	EEdgeBehaviour EdgeBehaviour,
	FEonformScalarField& OutField,
	FString* OutError)
{
	return ApplyPixels(Source, Custom, Strength * static_cast<float>(Source.Domain.Dimensions.X), DirectionDegrees, EdgeBehaviour, OutField, OutError);
}

void RegisterEonformDirectionalWarpNode()
{
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::DirectionalWarp;
	D.DisplayName = TEXT("DirectionalWarp");
	D.Category = TEXT("Modify");
	D.Description = TEXT("Warps Input along a fixed direction using a Custom scalar guide centered at 0.5.");
	D.Inputs.Add(Port(TEXT("Input"), TEXT("Input")));
	D.Inputs.Add(Port(TEXT("Custom"), TEXT("Custom")));
	D.Outputs.Add(Port(TEXT("Out"), TEXT("Out")));
	D.Parameters = {
		Number(TEXT("Strength"), TEXT("Strength"), 0.25, 0.0, 5.0),
		Number(TEXT("Direction"), TEXT("Direction"), 45.0, 0.0, 360.0),
		Choice(TEXT("EdgeBehaviour"), TEXT("Edge Behaviour"), TEXT("Mirror"), { TEXT("Edge"), TEXT("Mirror") })
	};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(D.Type, EvaluateDirectionalWarp);
}
