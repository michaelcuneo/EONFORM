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

	FGaeaTerrainParameterDescriptor TransformNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum, const TCHAR* Group = nullptr)
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

	FGaeaTerrainParameterDescriptor TransformBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor TransformNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
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

	float TransformBlend(float Original, float Transformed, FName BlendMode)
	{
		if (BlendMode == TEXT("Blend")) return FMath::Lerp(Original, Transformed, 0.5f);
		if (BlendMode == TEXT("Add")) return Original + Transformed;
		if (BlendMode == TEXT("Subtract")) return Original - Transformed;
		if (BlendMode == TEXT("Difference")) return FMath::Abs(Original - Transformed);
		if (BlendMode == TEXT("Multiply")) return Original * Transformed;
		if (BlendMode == TEXT("Screen"))
		{
			const float A = Original * 0.5f + 0.5f;
			const float B = Transformed * 0.5f + 0.5f;
			return (1.0f - (1.0f - A) * (1.0f - B)) * 2.0f - 1.0f;
		}
		if (BlendMode == TEXT("Max")) return FMath::Max(Original, Transformed);
		if (BlendMode == TEXT("Min")) return FMath::Min(Original, Transformed);
		return Transformed;
	}

	float TransformSampleField(const FGaeaScalarField& Source, FVector2d SamplePosition, FName Edges, FName Quality)
	{
		const FVector2d Min = Source.Domain.WorldMin;
		const FVector2d Max = Source.Domain.WorldMax;
		const bool bOutside = SamplePosition.X < Min.X || SamplePosition.X > Max.X || SamplePosition.Y < Min.Y || SamplePosition.Y > Max.Y;
		if (bOutside)
		{
			if (Edges == TEXT("None")) return 0.0f;
			SamplePosition.X = FMath::Clamp(SamplePosition.X, Min.X, Max.X);
			SamplePosition.Y = FMath::Clamp(SamplePosition.Y, Min.Y, Max.Y);
		}

		if (Quality != TEXT("Draft")) return Source.SampleWorld(SamplePosition, true);

		const FVector2d GridCoordinate = Source.Domain.WorldToStorageCoordinate(SamplePosition);
		const FIntPoint StorageDimensions = Source.Domain.GetStorageDimensions();
		const int32 X = FMath::Clamp(FMath::RoundToInt(GridCoordinate.X), 0, StorageDimensions.X - 1);
		const int32 Y = FMath::Clamp(FMath::RoundToInt(GridCoordinate.Y), 0, StorageDimensions.Y - 1);
		return Source.AtStorage(X, Y);
	}

	bool TransformField(const FGaeaTerrainNode& Node, const FGaeaScalarField& Source, bool bHeightField, FGaeaScalarField& OutField, FString& Error)
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
		const double OffsetX = Node.GetNumber(TEXT("OffsetX"), 0.0);
		const double OffsetY = Node.GetNumber(TEXT("OffsetY"), 0.0);
		const float OffsetZ = static_cast<float>(Node.GetNumber(TEXT("OffsetZ"), 0.0));
		const double AngleRadians = FMath::DegreesToRadians(Node.GetNumber(TEXT("Angle"), 0.0));
		const FName BlendMode = Node.GetName(TEXT("BlendMode"), TEXT("None"));
		const FName Edges = Node.GetName(TEXT("Edges"), TEXT("None"));
		const FName Quality = Node.GetName(TEXT("Quality"), TEXT("High"));
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
				const FVector2d SourceWorld(Center.X + RotatedX / ScaleX, Center.Y + RotatedY / ScaleY);
				float Transformed = TransformSampleField(Source, SourceWorld, Edges, Quality);
				if (bHeightField) Transformed += OffsetZ;

				if (Edges == TEXT("Thin") || Edges == TEXT("Wide") || Edges == TEXT("Soft"))
				{
					const double EdgeX = FMath::Min(SourceWorld.X - Source.Domain.WorldMin.X, Source.Domain.WorldMax.X - SourceWorld.X);
					const double EdgeY = FMath::Min(SourceWorld.Y - Source.Domain.WorldMin.Y, Source.Domain.WorldMax.Y - SourceWorld.Y);
					const double EdgeDistance = FMath::Min(EdgeX, EdgeY);
					const double Width = Edges == TEXT("Thin") ? 0.01 : Edges == TEXT("Wide") ? 0.08 : 0.04;
					const double WorldSpan = FMath::Min(Source.Domain.WorldMax.X - Source.Domain.WorldMin.X, Source.Domain.WorldMax.Y - Source.Domain.WorldMin.Y);
					float EdgeWeight = FMath::Clamp(static_cast<float>(EdgeDistance / FMath::Max(WorldSpan * Width, 1.0)), 0.0f, 1.0f);
					if (Edges == TEXT("Soft")) EdgeWeight = EdgeWeight * EdgeWeight * (3.0f - 2.0f * EdgeWeight);
					Transformed *= EdgeWeight;
				}

				const float Original = Source.AtInterior(X, Y);
				OutField.AtInterior(X, Y) = FMath::Clamp(TransformBlend(Original, Transformed, BlendMode), -1.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateTransformNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
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
			const bool bHeightField = FieldName == GaeaTerrainFieldNames::Height;
			if (!TransformField(Node, *SourceField, bHeightField, ResultField, Error) || !Dataset.SetScalarField(MoveTemp(ResultField)))
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
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Moves, scales, rotates, and blends terrain with configurable edge handling and quality.");
	Descriptor.Inputs.Add(TransformTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(TransformTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("OffsetX"), TEXT("Offset X"), 0.0, -1000000.0, 1000000.0, TEXT("Position")));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("OffsetY"), TEXT("Offset Y"), 0.0, -1000000.0, 1000000.0, TEXT("Position")));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("OffsetZ"), TEXT("Offset Z"), 0.0, -2.0, 2.0, TEXT("Position")));
	Descriptor.Parameters.Add(TransformBooleanParameter(TEXT("Uniform"), TEXT("Uniform"), true, TEXT("Scale")));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("Scale"), TEXT("Scale"), 1.0, 0.001, 10.0, TEXT("Scale")));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.001, 10.0, TEXT("Scale")));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.001, 10.0, TEXT("Scale")));
	Descriptor.Parameters.Add(TransformNumberParameter(TEXT("Angle"), TEXT("Angle"), 0.0, -360.0, 360.0, TEXT("Rotation")));
	Descriptor.Parameters.Add(TransformNameParameter(TEXT("BlendMode"), TEXT("Blend Mode"), TEXT("None"), { TEXT("None"), TEXT("Blend"), TEXT("Add"), TEXT("Subtract"), TEXT("Difference"), TEXT("Multiply"), TEXT("Screen"), TEXT("Max"), TEXT("Min") }, TEXT("Settings")));
	Descriptor.Parameters.Add(TransformNameParameter(TEXT("Edges"), TEXT("Edges"), TEXT("None"), { TEXT("None"), TEXT("Thin"), TEXT("Wide"), TEXT("Soft") }, TEXT("Settings")));
	Descriptor.Parameters.Add(TransformNameParameter(TEXT("Quality"), TEXT("Quality"), TEXT("High"), { TEXT("Draft"), TEXT("Medium"), TEXT("High") }, TEXT("Settings")));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Transform, EvaluateTransformNode);
}
