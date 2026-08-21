#include "GaeaTransformNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor TransformTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor TransformNumberParameter(
		FName Name,
		const TCHAR* DisplayName,
		double DefaultValue,
		double Minimum,
		double Maximum)
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

	FGaeaTerrainParameterDescriptor TransformBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	double TransformMirrorCoordinate(double Value, double Minimum, double Maximum)
	{
		const double Range = Maximum - Minimum;
		if (Range <= UE_DOUBLE_SMALL_NUMBER) return Minimum;
		const double Period = Range * 2.0;
		double Local = FMath::Fmod(Value - Minimum, Period);
		if (Local < 0.0) Local += Period;
		if (Local > Range) Local = Period - Local;
		return Minimum + Local;
	}

	float TransformSampleField(
		const FGaeaScalarField& Source,
		const FVector2d& WorldPosition,
		bool bUnfiltered,
		bool bFillEdges,
		bool bMirrorEdges)
	{
		FVector2d SamplePosition = WorldPosition;
		const FVector2d Min = Source.Domain.WorldMin;
		const FVector2d Max = Source.Domain.WorldMax;
		const bool bOutside = SamplePosition.X < Min.X || SamplePosition.X > Max.X || SamplePosition.Y < Min.Y || SamplePosition.Y > Max.Y;
		if (bOutside)
		{
			if (bMirrorEdges)
			{
				SamplePosition.X = TransformMirrorCoordinate(SamplePosition.X, Min.X, Max.X);
				SamplePosition.Y = TransformMirrorCoordinate(SamplePosition.Y, Min.Y, Max.Y);
			}
			else if (bFillEdges)
			{
				SamplePosition.X = FMath::Clamp(SamplePosition.X, Min.X, Max.X);
				SamplePosition.Y = FMath::Clamp(SamplePosition.Y, Min.Y, Max.Y);
			}
			else
			{
				return 0.0f;
			}
		}

		if (!bUnfiltered)
		{
			return Source.SampleWorld(SamplePosition, true);
		}

		const FVector2d GridCoordinate = Source.Domain.WorldToStorageCoordinate(SamplePosition);
		const FIntPoint StorageDimensions = Source.Domain.GetStorageDimensions();
		const int32 X = FMath::Clamp(FMath::RoundToInt(GridCoordinate.X), 0, StorageDimensions.X - 1);
		const int32 Y = FMath::Clamp(FMath::RoundToInt(GridCoordinate.Y), 0, StorageDimensions.Y - 1);
		return Source.AtStorage(X, Y);
	}

	bool TransformField(
		const FGaeaTerrainNode& Node,
		const FGaeaScalarField& Source,
		FGaeaScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Transform received an invalid terrain field.");
			return false;
		}

		const bool bUniform = Node.GetBool(TEXT("Uniform"), true);
		const double UniformScale = FMath::Max(Node.GetNumber(TEXT("Scale"), 1.0), 0.001);
		const double ScaleX = FMath::Max(bUniform ? UniformScale : Node.GetNumber(TEXT("ScaleX"), 1.0), 0.001);
		const double ScaleY = FMath::Max(bUniform ? UniformScale : Node.GetNumber(TEXT("ScaleY"), 1.0), 0.001);
		const bool bUnfiltered = Node.GetBool(TEXT("Unfiltered"), false);
		const bool bFillEdges = Node.GetBool(TEXT("FillEdges"), false);
		const bool bMirrorEdges = Node.GetBool(TEXT("MirrorEdges"), false);
		const double AngleRadians = FMath::DegreesToRadians(Node.GetNumber(TEXT("Angle"), 0.0));
		const double OffsetX = Node.GetNumber(TEXT("X"), 0.0);
		const double OffsetY = Node.GetNumber(TEXT("Y"), 0.0);
		const double CosAngle = FMath::Cos(AngleRadians);
		const double SinAngle = FMath::Sin(AngleRadians);
		const FVector2d Center = (Source.Domain.WorldMin + Source.Domain.WorldMax) * 0.5;

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const FVector2d DestinationWorld = Source.Domain.InteriorSampleToWorld(X, Y);
				const FVector2d Shifted = DestinationWorld - Center - FVector2d(OffsetX, OffsetY);
				const double RotatedX = CosAngle * Shifted.X + SinAngle * Shifted.Y;
				const double RotatedY = -SinAngle * Shifted.X + CosAngle * Shifted.Y;
				const FVector2d SourceWorld(
					Center.X + RotatedX / ScaleX,
					Center.Y + RotatedY / ScaleY);
				OutField.AtInterior(X, Y) = TransformSampleField(Source, SourceWorld, bUnfiltered, bFillEdges, bMirrorEdges);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateTransformNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Transform requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset;
		TArray<FName> FieldNames;
		Input->TerrainDataset.GetScalarFieldNames(FieldNames);
		for (const FName FieldName : FieldNames)
		{
			const FGaeaScalarField* SourceField = Input->TerrainDataset.FindScalarField(FieldName);
			if (!SourceField || !SourceField->IsValid())
			{
				Error = FString::Printf(TEXT("Transform input field '%s' is invalid."), *FieldName.ToString());
				return false;
			}

			FGaeaScalarField ResultField;
			if (!TransformField(Node, *SourceField, ResultField, Error) || !Dataset.SetScalarField(MoveTemp(ResultField)))
			{
				if (Error.IsEmpty()) Error = FString::Printf(TEXT("Transform could not publish field '%s'."), *FieldName.ToString());
				return false;
			}
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Transform produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaTransformNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Transform;
	Descriptor.DisplayName = TEXT("Transform");
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Scales, rotates, and offsets terrain with configurable edge handling and filtering.");
	Descriptor.Inputs.Add(TransformTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(TransformTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(TransformBooleanParameter(TEXT("Uniform"), TEXT("Uniform"), true));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("Scale"), TEXT("Scale"), 1.0, 0.001, 10.0));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.001, 10.0));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.001, 10.0));
	Descriptor.Parameters.Add(TransformBooleanParameter(TEXT("Unfiltered"), TEXT("Unfiltered"), false));
	Descriptor.Parameters.Add(TransformBooleanParameter(TEXT("FillEdges"), TEXT("Fill Edges"), false));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("Angle"), TEXT("Angle"), 0.0, -360.0, 360.0));
	Descriptor.Parameters.Add(TransformBooleanParameter(TEXT("MirrorEdges"), TEXT("Mirror Edges"), false));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("X"), TEXT("X"), 0.0, -1000000.0, 1000000.0));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("Y"), TEXT("Y"), 0.0, -1000000.0, 1000000.0));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Transform, EvaluateTransformNode);
}
