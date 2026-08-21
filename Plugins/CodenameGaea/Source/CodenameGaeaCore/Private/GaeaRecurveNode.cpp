#include "GaeaRecurveNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor RecurveTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor RecurveNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor RecurveIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum)
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
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor RecurveNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("Inward"));
		Parameter.NameOptions.Add(TEXT("Outward"));
		return Parameter;
	}

	float RecurveSampleClamped(const FGaeaScalarField& Field, int32 X, int32 Y)
	{
		const int32 SX = FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1);
		const int32 SY = FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1);
		return Field.AtInterior(SX, SY);
	}

	float RecurveNeighborhoodMean(const FGaeaScalarField& Field, int32 X, int32 Y, int32 Radius)
	{
		float Sum = 0.0f;
		int32 Count = 0;
		for (int32 DY = -Radius; DY <= Radius; ++DY)
		{
			for (int32 DX = -Radius; DX <= Radius; ++DX)
			{
				Sum += RecurveSampleClamped(Field, X + DX, Y + DY);
				++Count;
			}
		}
		return Count > 0 ? Sum / static_cast<float>(Count) : Field.AtInterior(X, Y);
	}

	bool RecurveHeightField(const FGaeaTerrainNode& Node, const FGaeaScalarField& Source, FGaeaScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Recurve received an invalid Height field.");
			return false;
		}

		const float Power = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Power"), 0.5)), 0.0f, 4.0f);
		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.0f, 1.0f);
		const int32 Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 1)), 1, 32);
		const FName Style = Node.GetName(TEXT("Style"), TEXT("Inward"));
		if (Style != TEXT("Inward") && Style != TEXT("Outward"))
		{
			Error = TEXT("Recurve Style must be Inward or Outward.");
			return false;
		}

		const int32 Radius = FMath::Clamp(1 + FMath::RoundToInt((1.0f - Scale) * 5.0f), 1, 6);
		const float Direction = Style == TEXT("Inward") ? 1.0f : -1.0f;

		FGaeaScalarField Current = Source;
		FGaeaScalarField Next = Source;
		for (int32 Pass = 0; Pass < Iterations; ++Pass)
		{
			for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
				{
					const float Center = Current.AtInterior(X, Y);
					const float Mean = RecurveNeighborhoodMean(Current, X, Y, Radius);
					const float Curvature = Center - Mean;
					const float ShapedCurvature = FMath::Sign(Curvature) * FMath::Pow(FMath::Abs(Curvature), 0.75f);
					Next.AtInterior(X, Y) = FMath::Clamp(Center + Direction * ShapedCurvature * Power, -1.0f, 1.0f);
				}
			}
			Swap(Current, Next);
		}

		OutField = MoveTemp(Current);
		return OutField.IsValid();
	}

	bool EvaluateRecurveNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Recurve requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Recurve terrain input has no valid Height field.");
			return false;
		}

		FGaeaScalarField ResultHeight;
		if (!RecurveHeightField(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Recurve could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Recurve produced an invalid terrain value.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaRecurveNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Recurve;
	Descriptor.DisplayName = TEXT("Recurve");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Curvature-based terrain expander that inflates or shrinks formations while preserving detail according to Scale.");
	Descriptor.Inputs.Add(RecurveTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(RecurveTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(RecurveNumberParameter(TEXT("Power"), TEXT("Power"), 0.5, 0.0, 4.0));
	Descriptor.Parameters.Add(RecurveNumberParameter(TEXT("Scale"), TEXT("Scale"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(RecurveIntegerParameter(TEXT("Iterations"), TEXT("Iterations"), 1, 1, 32));
	Descriptor.Parameters.Add(RecurveNameParameter(TEXT("Style"), TEXT("Style"), TEXT("Inward")));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Recurve, EvaluateRecurveNode);
}
