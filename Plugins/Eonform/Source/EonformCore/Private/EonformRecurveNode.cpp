#include "EonformRecurveNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor RecurveTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor RecurveNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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
		return Parameter;
	}

	FEonformTerrainParameterDescriptor RecurveIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum)
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
		return Parameter;
	}

	FEonformTerrainParameterDescriptor RecurveNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("Inward"));
		Parameter.NameOptions.Add(TEXT("Outward"));
		return Parameter;
	}

	float RecurveSampleClamped(const FEonformScalarField& Field, int32 X, int32 Y)
	{
		const int32 SX = FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1);
		const int32 SY = FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1);
		return Field.AtInterior(SX, SY);
	}

	float RecurveNeighborhoodMean(const FEonformScalarField& Field, int32 X, int32 Y, int32 Radius)
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

	bool RecurveHeightField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, FEonformScalarField& OutField, FString& Error)
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

		FEonformScalarField Current = Source;
		FEonformScalarField Next = Source;
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

	bool EvaluateRecurveNode(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Recurve requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Recurve terrain input has no valid Height field.");
			return false;
		}

		FEonformScalarField ResultHeight;
		if (!RecurveHeightField(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;

		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Recurve could not publish its Height field.");
			return false;
		}

		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Recurve produced an invalid terrain value.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterEonformRecurveNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Recurve;
	Descriptor.DisplayName = TEXT("Recurve");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Curvature-based terrain expander that inflates or shrinks formations while preserving detail according to Scale.");
	Descriptor.Inputs.Add(RecurveTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(RecurveTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(RecurveNumberParameter(TEXT("Power"), TEXT("Power"), 0.5, 0.0, 4.0));
	Descriptor.Parameters.Add(RecurveNumberParameter(TEXT("Scale"), TEXT("Scale"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(RecurveIntegerParameter(TEXT("Iterations"), TEXT("Iterations"), 1, 1, 32));
	Descriptor.Parameters.Add(RecurveNameParameter(TEXT("Style"), TEXT("Style"), TEXT("Inward")));

	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Recurve, EvaluateRecurveNode);
}
