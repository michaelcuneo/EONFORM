#include "GaeaZeroBordersNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor ZeroBordersTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor ZeroBordersNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FGaeaTerrainParameterDescriptor ZeroBordersIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum)
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

	FGaeaTerrainParameterDescriptor ZeroBordersNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("Round"));
		Parameter.NameOptions.Add(TEXT("Square"));
		Parameter.NameOptions.Add(TEXT("Precise"));
		return Parameter;
	}

	float ZeroBordersDistanceToEdge(int32 X, int32 Y, int32 Width, int32 Height, FName Style)
	{
		const float Left = static_cast<float>(X);
		const float Right = static_cast<float>(Width - 1 - X);
		const float Bottom = static_cast<float>(Y);
		const float Top = static_cast<float>(Height - 1 - Y);

		if (Style == TEXT("Round"))
		{
			const float CenterX = 0.5f * static_cast<float>(Width - 1);
			const float CenterY = 0.5f * static_cast<float>(Height - 1);
			const float NX = CenterX > 0.0f ? FMath::Abs(static_cast<float>(X) - CenterX) / CenterX : 0.0f;
			const float NY = CenterY > 0.0f ? FMath::Abs(static_cast<float>(Y) - CenterY) / CenterY : 0.0f;
			const float Radius = FMath::Sqrt(NX * NX + NY * NY);
			return FMath::Max(0.0f, 1.0f - Radius) * FMath::Min(CenterX, CenterY);
		}

		const float Horizontal = FMath::Min(Left, Right);
		const float Vertical = FMath::Min(Bottom, Top);
		return FMath::Min(Horizontal, Vertical);
	}

	bool ZeroBordersApplyToHeight(const FGaeaTerrainNode& Node, const FGaeaScalarField& Source, FGaeaScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Edge received an invalid Height field.");
			return false;
		}

		const FName Style = Node.GetName(TEXT("Style"), TEXT("Round"));
		if (Style != TEXT("Round") && Style != TEXT("Square") && Style != TEXT("Precise"))
		{
			Error = TEXT("Edge Style must be Round, Square, or Precise.");
			return false;
		}

		const int32 Width = Source.Domain.Dimensions.X;
		const int32 Height = Source.Domain.Dimensions.Y;
		const float MinDimension = static_cast<float>(FMath::Max(1, FMath::Min(Width, Height) - 1));
		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.1)), 0.0f, 1.0f);
		const int32 Pixels = FMath::Max(0, static_cast<int32>(Node.GetInteger(TEXT("Pixels"), 0)));
		const float Softness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Softness"), 0.5)), 0.0f, 1.0f);

		// Gaea exposes both normalized Size and an explicit Pixels control. The
		// public docs do not publish their internal precedence, so explicit pixel
		// sizing wins when non-zero while Size remains resolution-independent.
		const float EdgeSize = Pixels > 0
			? static_cast<float>(Pixels)
			: Size * MinDimension * 0.5f;
		const float Transition = FMath::Max(1.0f, FMath::Lerp(1.0f, FMath::Max(EdgeSize, 1.0f), Softness));
		const float SolidStart = FMath::Max(0.0f, EdgeSize - Transition);

		OutField = Source;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float Distance = ZeroBordersDistanceToEdge(X, Y, Width, Height, Style);
				float Weight = FMath::Clamp((Distance - SolidStart) / Transition, 0.0f, 1.0f);
				Weight = Weight * Weight * (3.0f - 2.0f * Weight);
				OutField.AtInterior(X, Y) = Source.AtInterior(X, Y) * Weight;
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateZeroBordersNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Edge requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Edge terrain input has no valid Height field.");
			return false;
		}

		FGaeaScalarField ResultHeight;
		if (!ZeroBordersApplyToHeight(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Edge could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Edge produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaZeroBordersNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::ZeroBorders;
	Descriptor.DisplayName = TEXT("Edge");
	Descriptor.Category = TEXT("Utility");
	Descriptor.Description = TEXT("Softly transitions terrain edges to zero. Known as Zero Borders in Gaea 1.");
	Descriptor.Inputs.Add(ZeroBordersTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(ZeroBordersTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ZeroBordersNameParameter(TEXT("Style"), TEXT("Style"), TEXT("Round")));
	Descriptor.Parameters.Add(ZeroBordersNumberParameter(TEXT("Size"), TEXT("Size"), 0.1, 0.0, 1.0));
	Descriptor.Parameters.Add(ZeroBordersIntegerParameter(TEXT("Pixels"), TEXT("Pixels"), 0, 0, 8192));
	Descriptor.Parameters.Add(ZeroBordersNumberParameter(TEXT("Softness"), TEXT("Softness"), 0.5, 0.0, 1.0));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::ZeroBorders, EvaluateZeroBordersNode);
}
